"""执行实际补丁库的接收生产者与任务；OS、队列和解析器边界使用确定性替身。"""
import ctypes
import re
import subprocess
import tempfile
from pathlib import Path

from llvmlite import binding as llvm
from host_c_test_lib import ROOT
from clock_manager_checks import run_clock_checks


FOLDER = ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol'


def read_ir(archive, directory, output):
    subprocess.run(['C:/JL/pi32/bin/llvm-ar.exe', 'x', str(archive), 'rdx_protocol.c.o'],
                   cwd=directory, check=True, capture_output=True, timeout=60)
    path = directory / output
    subprocess.run(['C:/JL/pi32/bin/clang.exe', '-S', '-emit-llvm', '-target',
                    'pi32v2', '-mcpu=r3', '-x', 'ir', str(directory / 'rdx_protocol.c.o'),
                    '-o', str(path)], check=True, capture_output=True, timeout=60)
    return path.read_text(encoding='utf-8')


def extract(ir, name):
    match = re.search(r'^define [^\n]*@' + re.escape(name) + r'\([^\n]*\).*?^}',
                      ir, re.M | re.S)
    assert match, name
    text = re.sub(r', ![a-z_.]+ !\d+| !dbg !\d+| #\d+', '', match[0])
    text = re.sub(r'^.*@llvm\.(?:dbg|lifetime)\.[^\n]*\n', '', text, flags=re.M)
    return text.replace('define internal ', 'define ')


class Packet(ctypes.Structure):
    _fields_ = [('data', ctypes.c_void_p), ('length', ctypes.c_int)]


class Receiver:
    def __init__(self, actual):
        self.callbacks = []
        self.reset()
        worker = extract(actual, 'rdx_protocol_recv_task')
        producer = extract(actual, 'rdx_protocol_packet_recv')
        # 仅在模拟的无限阻塞点退出主机执行；不改变实际队列消费、分派和自通知路径。
        wait = re.search(r'^  %3 = .*@os_sem_pend[^\n]*', worker, re.M)[0]
        worker = worker.replace(wait, wait + '''
  %host_blocked = icmp eq i32 %3, -999
  br i1 %host_blocked, label %host_stop, label %host_resume
host_stop:
  ret void
host_resume:
''')
        body = worker + '\n' + producer
        # 目标机以 byval 传递二字段结构；主机替身按地址读同一结构，避免跨 ABI 传参。
        body = body.replace(' byval', '')
        body = body.replace('@malloc(', '@host_malloc(').replace('@free(', '@host_free(')
        names = ('Queue', 'QueueNode', 'OS_SEM', 'DataPacket', 'BLE_RecvData')
        source = '\n'.join(re.search(r'^%struct\.' + name + r' = type .*$',
                                     actual, re.M)[0] for name in names)
        strings = sorted(set(re.findall(r'@((?:\.str|__FUNCTION__)[\w.]*)', body)))
        source += '\n' + '\n'.join(re.search(r'^@' + re.escape(name) + r' = .*$',
                                              actual, re.M)[0] for name in strings)
        source += '''
@ble_recv_data = global %struct.BLE_RecvData zeroinitializer
define i8 @host_busy() {
 %v = load i8, i8* getelementptr inbounds (%struct.BLE_RecvData, %struct.BLE_RecvData* @ble_recv_data, i32 0, i32 2)
 ret i8 %v
}
declare i8* @host_malloc(i32)
declare void @host_free(i8*)
declare void @log_print(i32, i8*, i8*, ...)
declare i32 @os_sem_pend(%struct.OS_SEM*, i32)
declare i32 @os_sem_post(%struct.OS_SEM*)
declare i32 @rdx_queue_getQueueSize(%struct.Queue*)
declare zeroext i1 @rdx_queue_isFull(%struct.Queue*)
declare zeroext i1 @rdx_queue_enqueue(%struct.Queue*, %struct.DataPacket*)
declare zeroext i1 @rdx_queue_dequeue(%struct.Queue*, %struct.DataPacket*)
declare zeroext i16 @rdx_ble_server_get_conn_handle()
declare void @rdx_protocol_recieve_data_handle(i8*, i16 zeroext)
declare void @clock_refurbish()
declare void @llvm.memcpy.p0i8.p0i8.i32(i8*, i8*, i32, i32, i1)
'''
        source += body
        ptr, integer, boolean = ctypes.c_void_p, ctypes.c_int, ctypes.c_bool
        self.bind('host_malloc', ptr, [integer], self.allocate)
        self.bind('host_free', None, [ptr], self.free)
        self.bind('log_print', None, [integer, ptr, ptr], lambda *args: None)
        self.bind('os_sem_pend', integer, [ptr, integer], self.pend)
        self.bind('os_sem_post', integer, [ptr], self.post)
        self.bind('rdx_queue_getQueueSize', integer, [ptr], lambda q: len(self.queue))
        self.bind('rdx_queue_isFull', boolean, [ptr], lambda q: len(self.queue) >= 15)
        self.bind('rdx_queue_enqueue', boolean, [ptr, ptr], self.enqueue)
        self.bind('rdx_queue_dequeue', boolean, [ptr, ptr], self.dequeue)
        self.bind('rdx_ble_server_get_conn_handle', ctypes.c_uint16, [], lambda: self.link)
        self.bind('rdx_protocol_recieve_data_handle', None, [ptr, ctypes.c_uint16], self.dispatch)
        self.bind('clock_refurbish', None, [], self.refresh)
        llvm.initialize_native_target()
        llvm.initialize_native_asmprinter()
        target = llvm.Target.from_default_triple().create_target_machine()
        module = llvm.parse_assembly(source)
        module.triple = llvm.get_default_triple()
        module.data_layout = str(target.target_data)
        module.verify()
        self.engine = llvm.create_mcjit_compiler(module, target)
        self.engine.finalize_object()
        self.worker = ctypes.CFUNCTYPE(None, ptr)(self.engine.get_function_address('rdx_protocol_recv_task'))
        self.receive = ctypes.CFUNCTYPE(integer, ptr, ctypes.c_uint16)(
            self.engine.get_function_address('rdx_protocol_packet_recv'))
        self.busy = ctypes.CFUNCTYPE(ctypes.c_uint8)(self.engine.get_function_address('host_busy'))

    def bind(self, name, result, args, function):
        callback = ctypes.CFUNCTYPE(result, *args)(function)
        self.callbacks.append(callback)
        llvm.add_symbol(name, ctypes.cast(callback, ctypes.c_void_p).value)

    def reset(self):
        self.queue, self.allocations, self.handled, self.errors = [], {}, [], []
        self.tokens = self.pends = self.posts = self.clocks = 0
        self.alloc_fail = self.enqueue_fail = self.dequeue_fail = self.post_error = 0
        self.on_wait = self.on_dispatch = None
        self.link = 1

    def allocate(self, size):
        if self.alloc_fail:
            return None
        value = ctypes.create_string_buffer(size)
        address = ctypes.addressof(value)
        self.allocations[address] = value
        return address

    def free(self, address):
        if address not in self.allocations:
            self.errors.append('invalid/double free')
        else:
            del self.allocations[address]

    def enqueue(self, queue, packet):
        if self.enqueue_fail:
            return False
        item = Packet.from_address(packet)
        # 库队列复制载荷，生产者入队后释放原缓冲；不能借用原指针掩盖所有权问题。
        copy = self.allocate(item.length)
        if not copy:
            return False
        ctypes.memmove(copy, item.data, item.length)
        self.queue.append((copy, item.length))
        return True

    def dequeue(self, queue, packet):
        if self.dequeue_fail:
            self.dequeue_fail -= 1
            return False
        if not self.queue:
            return False
        item = Packet.from_address(packet)
        item.data, item.length = self.queue.pop(0)
        return True

    def post(self, sem):
        self.posts += 1
        if self.post_error:
            return self.post_error
        # 小容量只用于覆盖计数饱和；饱和时已有令牌，不能当成通知丢失。
        if self.tokens >= 4:
            return 28
        self.tokens += 1
        return 0

    def pend(self, sem, timeout):
        self.pends += 1
        if timeout != 0:
            self.errors.append('timed idle wait')
        if self.pends > 1000:
            self.errors.append('worker failed to block')
            return -999
        if not self.tokens and self.on_wait:
            callback, self.on_wait = self.on_wait, None
            callback()
        if self.tokens:
            self.tokens -= 1
            return 0
        return -999

    def refresh(self):
        if not self.busy():
            self.errors.append('clock refreshed without admitted work')
        self.clocks += 1

    def dispatch(self, data, length):
        if not self.busy() or self.clocks != len(self.handled) + 1:
            self.errors.append('business starts without busy/clock admission')
        self.handled.append(ctypes.string_at(data, length))
        if self.on_dispatch:
            callback, self.on_dispatch = self.on_dispatch, None
            callback()

    def send(self, value):
        return self.receive(ctypes.create_string_buffer(value), len(value))

    def drain(self, expected):
        self.worker(None)
        assert not self.errors, self.errors
        assert self.handled == expected, self.handled
        assert self.clocks == len(expected)
        assert not self.busy() and not self.queue and not self.tokens and not self.allocations


def main():
    with tempfile.TemporaryDirectory(prefix='rdx_recv_') as work:
        directory = Path(work)
        actual = read_ir(FOLDER / 'librdxApp_patched.a', directory, 'actual.ll')
        original = read_ir(FOLDER / 'librdxApp.a', directory, 'original.ll')
    # 除接收任务外，业务提频调用与通知接线均不减少；单例不随逻辑退出销毁。
    clock_functions = re.findall(r'^define [^\n]*@([\w.]+)\([^\n]*\).*?^}', original, re.M | re.S)
    for name in clock_functions:
        if name != 'rdx_protocol_recv_task' and '@clock_refurbish(' in extract(original, name):
            assert extract(actual, name).count('@clock_refurbish(') == extract(original, name).count('@clock_refurbish('), name
    for name in ('rdx_protocol_task_create', 'rdx_protocol_task_free', 'rdx_protocol_file_cmd_handle'):
        assert extract(actual, name) == extract(original, name), name
    worker = extract(actual, 'rdx_protocol_recv_task')
    assert worker.count('@os_sem_post(') == 2 and worker.count('@clock_refurbish(') == 1
    os_errors = (ROOT / 'SDK/interface/system/os/os_error.h').read_text(encoding='utf-8')
    enumerators = re.search(r'enum\s*\{(.*?)\}', os_errors, re.S)[1].split(',')
    assert enumerators[28].strip() == 'OS_SEM_OVF'

    receiver = Receiver(actual)
    receiver.drain([])
    assert receiver.pends == 1 and receiver.posts == 0
    receiver.reset()
    receiver.tokens = 4
    receiver.drain([])
    assert receiver.pends == 5
    receiver.reset()
    assert receiver.send(b'first') == 5
    receiver.drain([b'first'])
    receiver.reset()
    receiver.on_wait = lambda: receiver.send(b'during-wait')
    receiver.drain([b'during-wait'])

    receiver.reset()
    for packet in (b'one', b'two', b'three'):
        assert receiver.send(packet) == len(packet)
    receiver.tokens = 1
    receiver.on_dispatch = lambda: receiver.send(b'during-dispatch')
    receiver.drain([b'one', b'two', b'three', b'during-dispatch'])
    receiver.reset()
    receiver.dequeue_fail = 1
    receiver.send(b'retry')
    receiver.drain([b'retry'])
    assert receiver.posts == 3

    for failure in ('alloc_fail', 'enqueue_fail'):
        receiver.reset()
        setattr(receiver, failure, 1)
        assert receiver.send(b'rejected') == 0
        receiver.drain([])
        assert receiver.posts == 0
    receiver.reset()
    assert receiver.receive(None, 1) == 0 and receiver.send(b'') == 0
    receiver.drain([])

    receiver.reset()
    for index in range(15):
        assert receiver.send(bytes([index])) == 1
    assert receiver.tokens == 4
    assert receiver.send(b'overflow-replacement') == 20
    receiver.drain([b'overflow-replacement'])

    receiver.reset()
    for index in range(15):
        assert receiver.send(bytes([index])) == 1
    receiver.enqueue_fail = 1
    assert receiver.send(b'rejected-after-clear') == 0
    receiver.drain([])

    # 无效信号量没有合法的轮询修复：返回失败，让退出屏障走已有 fail-closed。
    receiver.reset()
    receiver.post_error = 2
    assert receiver.send(b'barrier') == 0
    assert not receiver.tokens and len(receiver.queue) == 1
    receiver.worker(None)
    assert not receiver.handled and not receiver.clocks and not receiver.errors
    for address, _ in receiver.queue:
        receiver.free(address)
    receiver.reset()

    # 退出屏障通过同一生产者唤醒；连接句柄为零也必须排空旧包和屏障，再服务新连接。
    for link in (1, 0):
        receiver.reset()
        receiver.send(b'old-command')
        receiver.link = link
        assert receiver.send(b'lifecycle-barrier') == 17
        receiver.drain([b'old-command', b'lifecycle-barrier'])
        receiver.link = 2
        receiver.send(b'new-owner')
        receiver.drain([b'old-command', b'lifecycle-barrier', b'new-owner'])
    print('PASS: actual receive worker/producer, event wait, FIFO drain, business clock, failures and re-wake')
    run_clock_checks()


if __name__ == '__main__':
    main()
