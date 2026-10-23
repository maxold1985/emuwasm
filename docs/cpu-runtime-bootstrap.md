# BoxedWine CPU runtime bootstrap

The CPU API compile test passes, but this is **not yet** an x86 execution test.

## Verified upstream entry points

- `KMemory::create(KProcess*)` creates the address space; its constructor is private.
- `CPU::allocCPU(KMemory*)` creates a `NormalCPU`.
- `KThread(U32, const KProcessPtr&)` owns the CPU/thread execution context.
- `CPU::runNextSingleOp()` decodes and executes a single guest instruction.
- `KMemory::memcpy(U32, const void*, U32)` copies guest instruction bytes.

The interpreter accesses `cpu->thread` during instruction fetch and
`cpu->thread->process` during dispatch. Calling it with only a CPU and
an uninitialized `KMemory` will not work.

## First executable test (target)

Load these *32-bit x86* bytes into mapped executable guest memory:

```text
B8 0A 00 00 00        mov eax, 10
83 C0 14              add eax, 20
```

Set EIP to the mapped address. Execute exactly two operations through
`CPU::runNextSingleOp()`. Assert `cpu->reg[0].u32 == 30` and that EIP
advanced by eight bytes.

## Integration work remaining

1. Build the BoxedWine kernel/process/thread implementation and its dependencies,
   not just the CPU and soft-MMU translation units.
2. Construct a valid `KProcess`, `KMemory` and `KThread` using upstream
   initialization/ownership conventions.
3. Map executable guest pages, copy instruction bytes and initialize EIP.
4. Run the two upstream interpreter operations and assert registers/EIP.
5. Add this executable to CTest and run it in the CPU audit workflow.

**Do not replace the upstream interpreter with a handcrafted x86 switch**
to make this test pass. That would not validate the intended integration.
