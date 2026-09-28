#include "io.h"


uint8_t IO::Read(uint16_t address)
{
    if (const auto it = read_handlers.find(address); it != read_handlers.end())
        return it->second();

    if (address >= IO_LOW_START && address <= IO_LOW_END)
    {
        return this->io_low[address - IO_LOW_START];
    }

    if (address >= IO_HIGH_START && address <= IO_HIGH_END)
    {
        return this->io_high[address - IO_HIGH_START];
    }

    return 0xFF;
}

void IO::Write(uint16_t address, uint8_t value)
{
    if (const auto it = write_handlers.find(address); it != write_handlers.end())
    {
        it->second(value);
        return;
    }

    if (address >= IO_LOW_START && address <= IO_LOW_END)
    {
        this->io_low[address - IO_LOW_START] = value;
        return;
    }

    if (address >= IO_HIGH_START && address <= IO_HIGH_END)
    {
        this->io_high[address - IO_HIGH_START] = value;
        return;
    }
}

void* IO::Ptr(uint16_t address)
{
    if (address >= IO_LOW_START && address <= IO_LOW_END)
    {
        return &this->io_low[address - IO_LOW_START];
    }

    if (address >= IO_HIGH_START && address <= IO_HIGH_END)
    {
        return &this->io_high[address - IO_HIGH_START];
    }

    return nullptr;
}

void IO::RegisterReadHandler(uint16_t address, const ReadHandler& handler)
{
    this->read_handlers[address]  = handler;
}

void IO::RegisterWriteHandler(uint16_t address, const WriteHandler& handler)
{
    this->write_handlers[address] = handler;
}
