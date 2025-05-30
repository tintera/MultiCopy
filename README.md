# Multiprocess communication

Let's use one semaphore for both: Decidnig role and synchronizing shared memory creation.
Let's prefer our API to not contein OS dependent object (like semaphore)

## High level Inter-process communication

```mermaid
sequenceDiagram
    participant Src as Source
    participant Rd as Reader
    participant Wt as Writer
    participant Dest as Destination
    
    Rd ->> Src : Open
    Wt ->> Dest : Create/open
    par
        loop
            Src ->> Rd : Block
            Rd ->> Wt : Block
        end
    and
        loop
            Wt ->> Dest : Block
            Wt ->> Rd : Block (empty)
        end
    end
```

Notes: Reader amd writter are synchronized by semaphores. It's shown on later diagram.

## Decide process role

```mermaid
sequenceDiagram
    participant Program
    participant Role
    participant AppCount as AppCount + IsReader Semaphore(start == 0, max == 1)
    Program ->> Role: GetRole
    Role ->>+ AppCount: Create
    alt
        AppCount ->> Role: Created
        Role ->> Program: Role(Reader)
        Role ->> AppCount: ReleaseSemaphore
    else
        AppCount ->> Role: Joined
        Role ->> AppCount: WaitForSingleObject(0.5 second)
        alt
            AppCount ->> Role: Success == Signaled
            Role ->> Program: Writer
        else
            AppCount ->> Role: Timeout
            Role ->>+ Program : Too many instances
        end
    end
```

## Create shared memory object

```mermaid
sequenceDiagram
    participant Program
    participant DataTransfer
    participant OS as OS(Windows)
    Program ->> DataTransfer: Create
    DataTransfer ->> OS : GetSharedMemoryBlock (CreateFileMapping(INVALID_HANDLE_VALUE...)
    OS ->> DataTransfer : Handle + Pointer
    DataTransfer -> Program : SaharedMemoryPtr
    alt
        Program ->> DataTransfer: ReaderInit
    end
```

## Do copy

```mermaid
sequenceDiagram
    participant Reader 
    participant Writer
    participant Mem as DataTransfer

    par
        Reader ->> Mem: GetReaderInterface
        create participant RdMem as ReaderInterface
        Mem -> RdMem : Create
        Mem ->> Reader: ReaderInterface&
        loop
            RdMem ->> Reader: Empty Block&
            Reader ->> RdMem: Data Block& + Size
        end
    and
        Writer ->> Mem: GetWritterInterface
        create participant WtMem as WriterInterface
        Mem -> WtMem : Create
        Mem ->> Writer: WriterInterface&
        loop
            WtMem ->> Writer: Data Block& + Size
            Writer ->> WtMem: Empty Block&
        end
    end
```

## Notes

Shared memory in Windows: https://learn.microsoft.com/en-us/windows/win32/memory/creating-named-shared-memory

Using semaphores in WIndows: https://learn.microsoft.com/en-us/windows/win32/sync/using-semaphore-objects?redirectedfrom=MSDN