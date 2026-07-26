@{
    Version = 2
    SourceCommit = '128eb8d77f945ee6184fa10953e03f5e13c62186'
    BuildIdentity = ''
    Items = @(
        @{ Name = 'P10 source commit'; Kind = 'Commit'; Required = $true; Path = '.git'; Sha256 = '' }
        @{
            Name = 'Frozen librdxApp archive'
            Kind = 'Archive'
            Required = $true
            Path = 'SDK/apps/common/third_party_profile/rdx_protocol/librdxApp.a'
            Sha256 = 'c540d70540dc4d61e15d1ca13579cd2342d4ea972ff0a74a1afccc04b1ef4aca'
        }
        @{ Name = 'P10 final CC firmware'; Kind = 'Firmware'; Required = $true; Path = ''; Sha256 = '' }
        @{ Name = 'P10 final CC ELF'; Kind = 'Elf'; Required = $true; Path = ''; Sha256 = '' }
        @{ Name = 'P10 final CC map'; Kind = 'Map'; Required = $true; Path = ''; Sha256 = '' }
        @{ Name = 'P10 final CC clean-build log'; Kind = 'BuildLog'; Required = $true; Path = ''; Sha256 = '' }
        @{ Name = 'P10 persistence validation archive'; Kind = 'PersistenceReport'; Required = $true; Path = ''; Sha256 = '' }
        @{ Name = 'P10 target-board regression record'; Kind = 'BoardReport'; Required = $true; Path = ''; Sha256 = '' }
        @{ Name = 'P10 residual-issue classification'; Kind = 'IssueReport'; Required = $true; Path = ''; Sha256 = '' }
    )
}
