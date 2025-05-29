# Multiprocess communication

Let's use one semaphore for both: Decidnig role and synchronizing shared memory creation.
Let's prefer our API to not contein OS dependent object (like semaphore)

## Decide process role

Advantages: Clean interface for program.

Disadvantages: Role knows SharedMemory.

Maybe alternative: Role return OS-agnostic semaphore abstractions and program to give it to shared memory constructor together with role.

```mermaid
sequenceDiagram
    participant Program
    participant Role
    participant AppCount as AppCount + IsReader Semaphore(start == 0, max == 1)
    Participant SharedMemory
    participant OS as OS(Windows)
    Program ->> Role: GetRole
    Role ->>+ AppCount: CreateSemaphoreA 
    alt
        AppCount ->> Role: Handle ( == This thread is reader.)
        Role ->> SharedMemory: Create
        SharedMemory ->> OS: CreateFileMapping(INVALID_HANDLE_VALUE...
        OS ->> SharedMemory : Handle
        SharedMemory ->> Role: SharedMemory&
        Role ->> Program: Role(Reader, SharedMemory&)
        Role ->> AppCount: ReleaseSemaphore
        AppCount ->> Role: Non-zero / success
    else
        AppCount ->> Role: Handle + ERROR_ALREADY_EXISTS
        Role ->> AppCount: WaitForSingleObject(0.5 second)
        alt
            AppCount ->> Role: Success == Signaled ( == This thread is writter)
            Role ->> SharedMemory: GetExisting
            SharedMemory ->> OS: OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, szName);
            OS ->> SharedMemory: Handle
            SharedMemory ->> Role: SharedMemory&
            Role ->> Program: Role(Writer, SharedMemory&)
        else
            AppCount ->> Role: Timeout
            Role ->>+ Program : Too many instances
        end
    end
```

## Do copy

```mermaid
sequenceDiagram
    participant Reader 
    participant Writer
    participant Mem as SharedMemory
    participant RdMem as ReaderInterface
    participant WtMem as WriterInterface    
    par
        Reader ->> Mem: GetReaderInterface
        Mem ->> Reader: ReaderInterface&
        loop
            RdMem ->> Reader: Empty Block&
            Reader ->> RdMem: Data Block& + Size
        end
    and
        Writer ->> Mem: GetWritterInterface
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