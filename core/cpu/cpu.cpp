#include "cpu.h"


#include "core/utils/logger.h"
#include "instructions/set.h"

static InstructionSet instruction_set = {};

CPU::CPU(const std::shared_ptr<MemoryInterface>& memory)
{
    this->mem = memory;

    this->reg.PC = this->mem->Read16(CPU_VECTOR_RESET_ADDR);
}

bool print_instructions = false;

uint8_t CPU::Cycle()
{

    if (!sleep)
    {
        uint16_t exec_pc = this->reg.PC;
        const Instruction* instruction = instruction_set.Decode(*this);

        instruction->execute(*this);

        if (!instruction->branch_absolute)
            this->reg.PC += instruction->size;

        if (print_instructions)
            Log::Info("0x{:04X} {}", exec_pc, instruction->name);

        return instruction->cycles.Count();
    }

    return 1; // TODO investigate proper sleep modes and ticking for other components
}

void CPU::Interrupt(uint16_t vector_address)
{
    const uint16_t target_pc = mem->Read16(vector_address);
    if (target_pc == 0xFFFF)
        return;

    Push16(reg.flags.CCR << 8);
    Push16(reg.PC);

    reg.PC = target_pc;
    reg.flags.I = true;
    sleep = false;
}

void CPU::Push16(uint16_t value) const
{
    *reg.SP -= 2;
    mem->Write16(*reg.SP, value);
}

uint16_t CPU::Pop16() const
{
    const uint16_t value = mem->Read16(*reg.SP);
    *reg.SP += 2;

    return value;
}
