#define NOMINMAX
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <cfgmgr32.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

namespace {

constexpr UCHAR kReportId = 6;
constexpr USHORT kUsagePage = 0xFF00;
constexpr USHORT kUsage = 0x0001;
constexpr size_t kReportLength = 64;
constexpr size_t kReportBodyLength = 63;
constexpr size_t kPayloadLength = 61;

struct Options {
  bool list_only = false;
  bool test_set_output_report = false;
  DWORD timeout_ms = 2000;
  std::wstring path_filter;
  std::wstring trace_path;
};

struct DeviceInfo {
  std::wstring path;
  std::wstring manufacturer;
  std::wstring product;
  HIDD_ATTRIBUTES attributes{};
  HIDP_CAPS caps{};
};

std::string Narrow(const std::wstring& value) {
  if (value.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()), nullptr,
                                       0, nullptr, nullptr);
  std::string result(size, '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(),
                      static_cast<int>(value.size()), result.data(), size,
                      nullptr, nullptr);
  return result;
}

std::string Hex(const uint8_t* data, size_t size) {
  std::ostringstream stream;
  stream << std::hex << std::setfill('0');
  for (size_t i = 0; i < size; ++i) {
    if (i) stream << ' ';
    stream << std::setw(2) << static_cast<unsigned>(data[i]);
  }
  return stream.str();
}

std::string JsonEscape(const std::string& value) {
  std::ostringstream stream;
  for (unsigned char ch : value) {
    switch (ch) {
      case '\\': stream << "\\\\"; break;
      case '"': stream << "\\\""; break;
      case '\r': stream << "\\r"; break;
      case '\n': stream << "\\n"; break;
      case '\t': stream << "\\t"; break;
      default:
        if (ch < 0x20) {
          stream << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                 << static_cast<unsigned>(ch) << std::dec;
        } else {
          stream << ch;
        }
    }
  }
  return stream.str();
}

class Trace {
 public:
  explicit Trace(std::wstring path) : path_(std::move(path)) {}

  void Add(const std::string& event, const std::string& detail) {
    std::ostringstream line;
    line << "{\"event\":\"" << JsonEscape(event) << "\",\"detail\":\""
         << JsonEscape(detail) << "\"}";
    entries_.push_back(line.str());
    std::cout << event << ": " << detail << '\n';
  }

  void Flush(bool success) const {
    if (path_.empty()) return;
    std::ofstream file(path_, std::ios::binary | std::ios::trunc);
    file << "{\n  \"success\": " << (success ? "true" : "false")
         << ",\n  \"events\": [\n";
    for (size_t i = 0; i < entries_.size(); ++i) {
      file << "    " << entries_[i];
      if (i + 1 != entries_.size()) file << ',';
      file << '\n';
    }
    file << "  ]\n}\n";
  }

 private:
  std::wstring path_;
  std::vector<std::string> entries_;
};

HANDLE OpenDevice(const std::wstring& path, DWORD access, bool overlapped) {
  return CreateFileW(path.c_str(), access,
                     FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                     OPEN_EXISTING,
                     overlapped ? FILE_FLAG_OVERLAPPED : FILE_ATTRIBUTE_NORMAL,
                     nullptr);
}

bool ReadDeviceInfo(const std::wstring& path, DeviceInfo* info) {
  HANDLE handle = OpenDevice(path, 0, false);
  if (handle == INVALID_HANDLE_VALUE) return false;

  info->path = path;
  wchar_t text[256]{};
  if (HidD_GetManufacturerString(handle, text, sizeof(text))) {
    info->manufacturer = text;
  }
  std::memset(text, 0, sizeof(text));
  if (HidD_GetProductString(handle, text, sizeof(text))) {
    info->product = text;
  }
  info->attributes.Size = sizeof(info->attributes);
  PHIDP_PREPARSED_DATA preparsed = nullptr;
  bool ok = HidD_GetAttributes(handle, &info->attributes) != FALSE &&
            HidD_GetPreparsedData(handle, &preparsed) != FALSE &&
            HidP_GetCaps(preparsed, &info->caps) == HIDP_STATUS_SUCCESS;
  if (preparsed) HidD_FreePreparsedData(preparsed);
  CloseHandle(handle);
  return ok;
}

std::vector<DeviceInfo> EnumerateDevices() {
  GUID hid_guid{};
  HidD_GetHidGuid(&hid_guid);
  HDEVINFO set = SetupDiGetClassDevsW(
      &hid_guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  std::vector<DeviceInfo> devices;
  if (set == INVALID_HANDLE_VALUE) return devices;

  for (DWORD index = 0;; ++index) {
    SP_DEVICE_INTERFACE_DATA interface_data{};
    interface_data.cbSize = sizeof(interface_data);
    if (!SetupDiEnumDeviceInterfaces(set, nullptr, &hid_guid, index,
                                     &interface_data)) {
      if (GetLastError() == ERROR_NO_MORE_ITEMS) break;
      continue;
    }
    DWORD required = 0;
    SetupDiGetDeviceInterfaceDetailW(set, &interface_data, nullptr, 0,
                                     &required, nullptr);
    std::vector<uint8_t> storage(required);
    auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(
        storage.data());
    detail->cbSize = sizeof(*detail);
    if (!SetupDiGetDeviceInterfaceDetailW(set, &interface_data, detail,
                                          required, nullptr, nullptr)) {
      continue;
    }
    DeviceInfo info;
    if (ReadDeviceInfo(detail->DevicePath, &info)) devices.push_back(info);
  }
  SetupDiDestroyDeviceInfoList(set);
  return devices;
}

bool JsonComplete(const std::string& value) {
  int depth = 0;
  bool started = false;
  bool in_string = false;
  bool escaped = false;
  bool complete = false;
  for (unsigned char ch : value) {
    if (complete) {
      if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') return false;
      continue;
    }
    if (!started) {
      if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
      if (ch != '{') return false;
      started = true;
      depth = 1;
      continue;
    }
    if (in_string) {
      if (escaped) escaped = false;
      else if (ch == '\\') escaped = true;
      else if (ch == '"') in_string = false;
      continue;
    }
    if (ch == '"') in_string = true;
    else if (ch == '{' || ch == '[') ++depth;
    else if (ch == '}' || ch == ']') {
      if (--depth < 0) return false;
      if (depth == 0) complete = true;
    }
  }
  return started && complete && !in_string && depth == 0;
}

std::vector<std::vector<uint8_t>> Frame(const std::string& json) {
  std::string payload = json;
  payload.push_back('\n');
  std::vector<std::vector<uint8_t>> reports;
  for (size_t offset = 0; offset < payload.size(); offset += kPayloadLength) {
    const size_t chunk = std::min(kPayloadLength, payload.size() - offset);
    std::vector<uint8_t> report(kReportLength, 0);
    report[0] = kReportId;
    report[1] = 0x02;
    report[2] = static_cast<uint8_t>(chunk);
    std::memcpy(report.data() + 3, payload.data() + offset, chunk);
    reports.push_back(std::move(report));
  }
  return reports;
}

bool WriteReports(HANDLE handle,
                  const std::vector<std::vector<uint8_t>>& reports,
                  Trace* trace) {
  for (const auto& report : reports) {
    DWORD written = 0;
    if (!WriteFile(handle, report.data(), static_cast<DWORD>(report.size()),
                   &written, nullptr) || written != report.size()) {
      trace->Add("write_file", "failed error=" + std::to_string(GetLastError()));
      return false;
    }
    trace->Add("write_file", Hex(report.data(), report.size()));
  }
  return true;
}

bool TestSetOutputReport(HANDLE handle,
                         const std::vector<uint8_t>& report,
                         Trace* trace) {
  const bool ok = HidD_SetOutputReport(
      handle, const_cast<uint8_t*>(report.data()),
      static_cast<ULONG>(report.size())) != FALSE;
  trace->Add("hid_set_output_report",
             std::string(ok ? "success " : "failed ") +
                 "error=" + std::to_string(GetLastError()));
  return ok;
}

bool ReadResponse(HANDLE handle, DWORD timeout_ms, std::string* json,
                  Trace* trace) {
  OVERLAPPED ov{};
  ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!ov.hEvent) return false;

  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
  bool success = false;
  while (std::chrono::steady_clock::now() < deadline) {
    std::vector<uint8_t> report(kReportLength, 0);
    DWORD read = 0;
    ResetEvent(ov.hEvent);
    BOOL started = ReadFile(handle, report.data(),
                            static_cast<DWORD>(report.size()), &read, &ov);
    if (!started && GetLastError() != ERROR_IO_PENDING) {
      trace->Add("read_file", "start failed error=" +
                                      std::to_string(GetLastError()));
      break;
    }
    DWORD remaining = static_cast<DWORD>(std::max<int64_t>(
        0, std::chrono::duration_cast<std::chrono::milliseconds>(
               deadline - std::chrono::steady_clock::now()).count()));
    DWORD wait = WaitForSingleObject(ov.hEvent, remaining);
    if (wait != WAIT_OBJECT_0 || !GetOverlappedResult(handle, &ov, &read, FALSE)) {
      CancelIoEx(handle, &ov);
      trace->Add("read_file", wait == WAIT_TIMEOUT ? "timeout" : "failed");
      break;
    }
    trace->Add("read_file", Hex(report.data(), read));
    if (read != kReportLength || report[0] != kReportId ||
        report[1] != 0x02 || report[2] > kPayloadLength ||
        static_cast<size_t>(report[2]) + 3 > read) {
      trace->Add("framing", "invalid input report");
      break;
    }
    json->append(reinterpret_cast<const char*>(report.data() + 3), report[2]);
    const size_t newline = json->find_first_of("\r\n");
    const std::string candidate = newline == std::string::npos
                                      ? *json
                                      : json->substr(0, newline);
    if (JsonComplete(candidate)) {
      *json = candidate;
      success = true;
      break;
    }
  }
  CloseHandle(ov.hEvent);
  return success;
}

bool ParseOptions(int argc, wchar_t** argv, Options* options) {
  for (int i = 1; i < argc; ++i) {
    std::wstring arg = argv[i];
    if (arg == L"--list") options->list_only = true;
    else if (arg == L"--test-set-output-report")
      options->test_set_output_report = true;
    else if ((arg == L"--path" || arg == L"--trace" ||
              arg == L"--timeout-ms") && i + 1 < argc) {
      std::wstring value = argv[++i];
      if (arg == L"--path") options->path_filter = value;
      else if (arg == L"--trace") options->trace_path = value;
      else options->timeout_ms = std::stoul(value);
    } else {
      return false;
    }
  }
  return true;
}

void PrintUsage() {
  std::cout << "Usage: codex_micro_hid [--list] [--path substring] "
               "[--timeout-ms 2000] [--trace file.json] "
               "[--test-set-output-report]\n";
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  Options options;
  if (!ParseOptions(argc, argv, &options)) {
    PrintUsage();
    return 2;
  }
  Trace trace(options.trace_path);
  const auto devices = EnumerateDevices();
  const DeviceInfo* selected = nullptr;
  for (const auto& device : devices) {
    std::ostringstream detail;
    detail << Narrow(device.path) << " vid=" << std::hex << std::setw(4)
           << std::setfill('0') << device.attributes.VendorID << " pid="
           << std::setw(4) << device.attributes.ProductID << " ver="
           << std::setw(4) << device.attributes.VersionNumber << " usage="
           << std::setw(4) << device.caps.UsagePage << ':' << std::setw(4)
           << device.caps.Usage << std::dec << " input="
           << device.caps.InputReportByteLength << " output="
           << device.caps.OutputReportByteLength;
    detail << " manufacturer=" << Narrow(device.manufacturer)
           << " product=" << Narrow(device.product);
    trace.Add("hid_interface", detail.str());

    const bool filter_match = options.path_filter.empty() ||
        device.path.find(options.path_filter) != std::wstring::npos;
    if (!selected && filter_match && device.caps.UsagePage == kUsagePage &&
        device.caps.Usage == kUsage) {
      selected = &device;
    }
  }
  if (options.list_only) {
    trace.Flush(true);
    return 0;
  }
  if (!selected) {
    trace.Add("selection", "no Usage FF00:0001 HID interface found");
    trace.Flush(false);
    return 3;
  }
  if (selected->caps.InputReportByteLength != kReportLength ||
      selected->caps.OutputReportByteLength != kReportLength) {
    trace.Add("caps", "expected 64-byte input and output reports");
    trace.Flush(false);
    return 4;
  }

  HANDLE write_handle = OpenDevice(selected->path, GENERIC_WRITE, false);
  HANDLE read_handle = OpenDevice(selected->path, GENERIC_READ, true);
  if (write_handle == INVALID_HANDLE_VALUE || read_handle == INVALID_HANDLE_VALUE) {
    trace.Add("open", "failed error=" + std::to_string(GetLastError()));
    if (write_handle != INVALID_HANDLE_VALUE) CloseHandle(write_handle);
    if (read_handle != INVALID_HANDLE_VALUE) CloseHandle(read_handle);
    trace.Flush(false);
    return 5;
  }

  const auto reports = Frame("{\"method\":\"device.status\",\"id\":1}");
  if (options.test_set_output_report && !reports.empty()) {
    TestSetOutputReport(write_handle, reports.front(), &trace);
  }
  bool success = WriteReports(write_handle, reports, &trace);
  std::string response;
  if (success) success = ReadResponse(read_handle, options.timeout_ms,
                                      &response, &trace);
  if (success) {
    trace.Add("device.status", response);
    success = response.find("\"id\":1") != std::string::npos &&
              response.find("\"layer_index\":1") != std::string::npos &&
              response.find("\"version\":\"t2620-mvp\"") !=
                  std::string::npos;
    if (!success) trace.Add("validation", "unexpected response fields");
  }

  CloseHandle(read_handle);
  CloseHandle(write_handle);
  trace.Flush(success);
  return success ? 0 : 6;
}
