#pragma once
#include <unordered_map>
#include <vector>
#include <stdexcept>
#include <cstdint>
#include "instruction.h"

struct InstructionTable;

using KeyPredicate = uint32_t(*)(const CPU&);
using ContainerBuilder = void(*)(InstructionTable&);

struct PatternEntry
{
    uint32_t first_value;
    uint32_t first_mask;
    uint32_t second_value;
    uint32_t second_mask;
    Instruction instruction;

    bool Matches(uint32_t first, uint32_t second) const;
};

struct InstructionTable
{
    KeyPredicate first_key;
    KeyPredicate second_key;

    std::unordered_map<uint32_t, std::unordered_map<uint32_t, Instruction>> entries;
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, InstructionTable*>> subtables;
    std::vector<PatternEntry> patterns;

    InstructionTable(KeyPredicate first, KeyPredicate second);

    void Add(uint32_t first, uint32_t second, Instruction instr);
    void Add(uint32_t first, std::vector<uint32_t> seconds, Instruction instr);
    void AddSubtable(uint32_t first, uint32_t second, InstructionTable& table);
    void AddSubtable(uint32_t first, uint32_t second, KeyPredicate first_key, KeyPredicate second_key, ContainerBuilder builder);
    void AddPattern(uint32_t first_value, uint32_t first_mask, uint32_t second_value, uint32_t second_mask, Instruction instr);
    const Instruction* Lookup(uint32_t first, uint32_t second) const;
    const Instruction* Decode(const CPU& cpu) const;
};
