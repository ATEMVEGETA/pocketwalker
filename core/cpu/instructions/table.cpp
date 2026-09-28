#include "table.h"

#include <cstdio>

#include "../cpu.h"

bool PatternEntry::Matches(const uint32_t first, const uint32_t second) const
{
    return (first & first_mask) == first_value &&
           (second & second_mask) == second_value;
}

InstructionTable::InstructionTable(const KeyPredicate first, const KeyPredicate second)
    : first_key(first), second_key(second) {}

void InstructionTable::Add(const uint32_t first, const uint32_t second, Instruction instr)
{
    entries[first][second] = std::move(instr);
}

void InstructionTable::Add(uint32_t first, std::vector<uint32_t> seconds, Instruction instr)
{
    for (const uint32_t second : seconds)
        entries[first][second] = instr;
}

void InstructionTable::AddSubtable(const uint32_t first, const uint32_t second, InstructionTable& table)
{
    subtables[first][second] = &table;
}

void InstructionTable::AddSubtable(uint32_t first, uint32_t second, KeyPredicate first_key, KeyPredicate second_key,
    ContainerBuilder builder)
{
    const auto table = new InstructionTable {
        first_key,
        second_key
    };

    builder(*table);

    subtables[first][second] = table;
}

void InstructionTable::AddPattern(const uint32_t first_value, const uint32_t first_mask,
                                  const uint32_t second_value, const uint32_t second_mask,
                                  Instruction instr)
{
    patterns.push_back({first_value, first_mask, second_value, second_mask, std::move(instr)});
}

const Instruction* InstructionTable::Lookup(const uint32_t first, const uint32_t second) const
{
    if (auto it1 = entries.find(first); it1 != entries.end())
        if (auto it2 = it1->second.find(second); it2 != it1->second.end())
            return &it2->second;

    for (const auto& p : patterns)
        if (p.Matches(first, second)) return &p.instruction;

    return nullptr;
}

const Instruction* InstructionTable::Decode(const CPU& cpu) const
{
    const uint32_t first = first_key(cpu);
    const uint32_t second = second_key(cpu);

    if (const auto it1 = subtables.find(first); it1 != subtables.end())
        if (const auto it2 = it1->second.find(second); it2 != it1->second.end())
            return it2->second->Decode(cpu);

    if (const auto* instr = Lookup(first, second))
        return instr;

    std::fprintf(stderr, "Invalid Instruction at 0x%04X: 0x%02X 0x%02X\n",
                 cpu.reg.PC, first, second);
    exit(1);
}
