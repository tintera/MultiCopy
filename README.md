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
            Src ->> Rd : Data to Block
            Rd ->> Wt : Block&
        end
    and
        loop
            Wt ->> Dest : Data from Block
            Wt ->> Rd : Block& (empty)
        end
    end
```

Notes:
Reader owns all blocks at this diagram start.

Reader and writter are decided and sychronized by semaphores. It's shown on later diagrams.

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

## Final handshake

Writer part is done before writting file, reader part is done after reading whole file.
It's important for files smaller than shared memory block, where reader can have its work
done before writter starts.

Without this handshake reader might finish before second process starts deciding it's role.
And therefore it would find it's the first/only process and become reader again.

```mermaid
sequenceDiagram
    participant OS as OS (Operating Syste/Shell)
    participant Reader     
    participant Sem as Semaphore(Finisher)
    participant Writer
    par
        Reader ->> Sem: Create(name = finisher)
        Reader ->> Sem: Wait
    and
        Writer ->> Sem: Create(name = finisher)
        Writer ->> Sem: Release
        Writer ->> OS: Success
    end
    Sem ->> Reader: Released
    Reader ->> OS: Success
```

## Not used version of the final handshake

Start condition: Reader is done readng source file.
(Posible optimizazion: when more than BLOCK_NUM time it's sure writter exists as it returned at least one block)

<https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createeventa>
Says: Use the CloseHandle function to close the handle. The system closes the handle automatically when the process terminates. The event object is destroyed when its last handle has been closed.

```mermaid
sequenceDiagram
    participant OS as OS (Operating Syste/Shell)
    participant Reader
    participant Event as Event(WriterStarted)
    participant Writer
    par
        Reader ->> Event: Wait for this event
    and
        Writer ->> Event: Send/Create
        Writer ->> OS: Success
    end
    Event ->> Reader: Exists/Found
    Reader ->> OS: Success
```

This handshake is needed for example for small files.
We need to prevent the situation when Reader is finished before writter is even started. Without this handshake both processes would become readers.

What is the problem here? When event is not consumed by reader until writer ends, event is disposed and reader wits indefinitely.

Solution: Use somethning created by reader:
1) New semaphore created by reader. And wait for it. Writer will release it.
2) After sending the event write a status flag also into shared memory.
3) Expect at least one block to be returned from writter. <-- Selected solution

## Notes

Shared memory in Windows: https://learn.microsoft.com/en-us/windows/win32/memory/creating-named-shared-memory

Using semaphores in WIndows: https://learn.microsoft.com/en-us/windows/win32/sync/using-semaphore-objects?redirectedfrom=MSDN