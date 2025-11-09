#include "register_allocator.h"

#include <cassert>

RegisterAllocator::RegisterAllocator(size_t num_physical_registers)
{
  assert(num_physical_registers <= std::numeric_limits<PHYSICAL_REGISTER_ID>::max());
  for (size_t i = 0; i < num_physical_registers; ++i) {
    free_registers.push(static_cast<PHYSICAL_REGISTER_ID>(i));
  }
  physical_register_file = std::vector<physical_register>(num_physical_registers, {0, 0, false, false});
  frontend_RAT.fill(-1); // default value for no mapping
  backend_RAT.fill(-1);
}

PHYSICAL_REGISTER_ID RegisterAllocator::rename_dest_register(int16_t reg, champsim::program_ordered<ooo_model_instr>::id_type producer_id)
{
  assert(!free_registers.empty());

  PHYSICAL_REGISTER_ID phys_reg = free_registers.front();
  free_registers.pop();
  frontend_RAT[reg] = phys_reg;
  physical_register_file.at(phys_reg) = {(uint16_t)reg, producer_id, false, true}; // arch_reg_index, valid, busy

  return phys_reg;
}

PHYSICAL_REGISTER_ID RegisterAllocator::rename_src_register(int16_t reg)
{
  PHYSICAL_REGISTER_ID phys = frontend_RAT[reg];

  if (phys < 0) {
    // allocate the register if it hasn't yet been mapped
    // (common due to the traces being slices in the middle of a program)
    phys = free_registers.front();
    free_registers.pop();
    frontend_RAT[reg] = phys;
    backend_RAT[reg] = phys;                                          // we assume this register's last write has been committed
    physical_register_file.at(phys) = {(uint16_t)reg, 0, true, true}; // arch_reg_index, producing_inst_id, valid, busy
  }

  return phys;
}

void RegisterAllocator::complete_dest_register(PHYSICAL_REGISTER_ID physreg)
{
  // mark the physical register as valid
  physical_register_file.at(physreg).valid = true;
}

void RegisterAllocator::retire_dest_register(PHYSICAL_REGISTER_ID physreg)
{
  // grab the arch reg index, find old phys reg in backend RAT
  uint16_t arch_reg = physical_register_file.at(physreg).arch_reg_index;
  PHYSICAL_REGISTER_ID old_phys_reg = backend_RAT[arch_reg];

  // update the backend RAT with the new phys reg
  backend_RAT[arch_reg] = physreg;

  // free the old phys reg
  if (old_phys_reg != -1) {
    free_register(old_phys_reg);
  }
}

void RegisterAllocator::free_register(PHYSICAL_REGISTER_ID physreg)
{
  physical_register_file.at(physreg) = {255, 0, false, false}; // arch_reg_index, producing_inst_id, valid, busy
  free_registers.push(physreg);
}

bool RegisterAllocator::isValid(PHYSICAL_REGISTER_ID physreg) const { return physical_register_file.at(physreg).valid; }

bool RegisterAllocator::isAllocated(PHYSICAL_REGISTER_ID archreg) const { return frontend_RAT[archreg] != -1; }

unsigned long RegisterAllocator::count_free_registers() const { return std::size(free_registers); }

int RegisterAllocator::count_reg_dependencies(const ooo_model_instr& instr) const
{
  return static_cast<int>(std::count_if(std::begin(instr.source_registers), std::end(instr.source_registers), [this](auto reg) { return !isValid(reg); }));
}

void RegisterAllocator::reset_frontend_RAT()
{
  std::copy(std::begin(backend_RAT), std::end(backend_RAT), std::begin(frontend_RAT));
  // once wrong path is implemented:
  // find registers allocated by wrong-path instructions and free them
}

void RegisterAllocator::print_deadlock()
{
  fmt::print("Frontend Register Allocation Table        Backend Register Allocation Table\n");
  for (size_t i = 0; i < frontend_RAT.size(); ++i) {
    fmt::print("Arch reg: {:3}    Phys reg: {:3}            Arch reg: {:3}    Phys reg: {:3}\n", i, frontend_RAT[i], i, backend_RAT[i]);
  }

  if (count_free_registers() == 0) {
    fmt::print("\n**WARNING!! WARNING!!** THE PHYSICAL REGISTER FILE IS COMPLETELY OCCUPIED.\n");
    fmt::print("It is extremely likely your register file size is too small.\n");
  }

  fmt::print("\nPhysical Register File\n");
  for (size_t i = 0; i < physical_register_file.size(); ++i) {
    fmt::print("Phys reg: {:3}\t Arch reg: {:3}\t Producer: {}\t Valid: {}\t Busy: {}\n", static_cast<int>(i),
               static_cast<int>(physical_register_file.at(i).arch_reg_index), physical_register_file.at(i).producing_instruction_id,
               physical_register_file.at(i).valid, physical_register_file.at(i).busy);
  }
  fmt::print("\n");
}

//ADDED
// ==========================================
// *** NEW VALUE PREDICTION FUNCTIONS ***
// ==========================================

PHYSICAL_REGISTER_ID RegisterAllocator::get_current_mapping(int16_t arch_reg) const
{
  if (arch_reg < 0 || arch_reg >= frontend_RAT.size()) {
    return -1; // Invalid architectural register
  }
  return frontend_RAT[arch_reg];
}

void RegisterAllocator::record_rename(uint64_t instr_id, int16_t arch_reg, 
                                      PHYSICAL_REGISTER_ID old_phys_reg, 
                                      PHYSICAL_REGISTER_ID new_phys_reg)
{
  rename_history[instr_id].push_back({arch_reg, old_phys_reg, new_phys_reg, instr_id});
  
  if constexpr (champsim::debug_print) {
    fmt::print("[REG_ALLOC] Record rename instr_id: {} arch_reg: {} {} -> {}\n",
               instr_id, arch_reg, old_phys_reg, new_phys_reg);
  }
}

void RegisterAllocator::undo_rename(uint64_t instr_id)
{
  auto it = rename_history.find(instr_id);
  if (it == rename_history.end()) {
    // No rename history for this instruction (might not have dest registers)
    if constexpr (champsim::debug_print) {
      fmt::print("[REG_ALLOC] No rename history for instr_id: {}\n", instr_id);
    }
    return;
  }

  if constexpr (champsim::debug_print) {
    fmt::print("[REG_ALLOC] Undo rename for instr_id: {} ({} mappings)\n",
               instr_id, it->second.size());
  }

  // Process each rename in reverse order (LIFO - last renamed first)
  for (auto rit = it->second.rbegin(); rit != it->second.rend(); ++rit) {
    const auto& checkpoint = *rit;
    
    if constexpr (champsim::debug_print) {
      fmt::print("[REG_ALLOC]   Restoring arch_reg: {} from {} back to {}\n",
                 checkpoint.arch_reg, checkpoint.new_phys_reg, checkpoint.old_phys_reg);
    }

    // 1. Restore frontend RAT to point back to the old physical register
    frontend_RAT[checkpoint.arch_reg] = checkpoint.old_phys_reg;
    
    // 2. Mark the "new" physical register as invalid/not-busy
    //    (it was speculatively allocated but now we're undoing that)
    if (checkpoint.new_phys_reg >= 0 && checkpoint.new_phys_reg < physical_register_file.size()) {
      physical_register_file[checkpoint.new_phys_reg].valid = false;
      physical_register_file[checkpoint.new_phys_reg].busy = false;
      physical_register_file[checkpoint.new_phys_reg].arch_reg_index = 255;
      physical_register_file[checkpoint.new_phys_reg].producing_instruction_id = 0;
    }
    
    // 3. Return the "new" physical register to the free list
    free_registers.push(checkpoint.new_phys_reg);
    
    if constexpr (champsim::debug_print) {
      fmt::print("[REG_ALLOC]   Returned phys_reg {} to free list (now {} free)\n",
                 checkpoint.new_phys_reg, free_registers.size());
    }
  }

  // Remove the rename history for this instruction
  rename_history.erase(it);
}

void RegisterAllocator::retire_rename(uint64_t instr_id)
{
  auto it = rename_history.find(instr_id);
  if (it != rename_history.end()) {
    if constexpr (champsim::debug_print) {
      fmt::print("[REG_ALLOC] Clear rename history for retired instr_id: {}\n", instr_id);
    }
    rename_history.erase(it);
  }
}

uint64_t RegisterAllocator::get_producer(PHYSICAL_REGISTER_ID phys_reg) const
{
  if (phys_reg >= 0 && phys_reg < physical_register_file.size()) {
    return physical_register_file[phys_reg].producing_instruction_id;
  }
  return 0;
}

void RegisterAllocator::print_rat_state() const
{
  if constexpr (champsim::debug_print) {
    fmt::print("[REG_ALLOC] RAT State:\n");
    for (size_t i = 0; i < frontend_RAT.size(); ++i) {
      if (frontend_RAT[i] != -1) {  // Only print allocated mappings
        PHYSICAL_REGISTER_ID phys = frontend_RAT[i];
        fmt::print("  arch_reg {:3} -> phys_reg {:3} (valid: {}, busy: {}, producer: {})\n",
                   i, phys, 
                   physical_register_file[phys].valid,
                   physical_register_file[phys].busy,
                   physical_register_file[phys].producing_instruction_id);
      }
    }
    fmt::print("  Free registers: {}\n", free_registers.size());
  }
}

void RegisterAllocator::print_rename_history() const
{
  if constexpr (champsim::debug_print) {
    fmt::print("[REG_ALLOC] Rename History ({} instructions):\n", rename_history.size());
    for (const auto& [instr_id, checkpoints] : rename_history) {
      fmt::print("  instr_id {}: {} renames\n", instr_id, checkpoints.size());
      for (const auto& cp : checkpoints) {
        fmt::print("    arch_reg {:3} : {:3} -> {:3}\n", 
                   cp.arch_reg, cp.old_phys_reg, cp.new_phys_reg);
      }
    }
  }
}

void RegisterAllocator::validate_state() const
{
  // Check for duplicate physical registers in free list
  std::queue<PHYSICAL_REGISTER_ID> temp_queue = free_registers;
  std::unordered_set<PHYSICAL_REGISTER_ID> free_set;
  
  while (!temp_queue.empty()) {
    PHYSICAL_REGISTER_ID phys = temp_queue.front();
    temp_queue.pop();
    
    if (free_set.count(phys)) {
      fmt::print("[REG_ALLOC] ERROR: Duplicate phys_reg {} in free list!\n", phys);
    }
    free_set.insert(phys);
    
    // Check that free registers are not marked as busy/valid
    if (phys >= 0 && phys < physical_register_file.size()) {
      if (physical_register_file[phys].valid || physical_register_file[phys].busy) {
        fmt::print("[REG_ALLOC] ERROR: Phys reg {} is in free list but marked valid/busy!\n", phys);
      }
    }
  }

  // Check rename history consistency
  for (const auto& [instr_id, checkpoints] : rename_history) {
    for (const auto& cp : checkpoints) {
      // The new physical register should not be in the free list
      if (free_set.count(cp.new_phys_reg)) {
        fmt::print("[REG_ALLOC] ERROR: instr {} allocated phys_reg {} but it's in free list!\n",
                   instr_id, cp.new_phys_reg);
      }
    }
  }
  
  if constexpr (champsim::debug_print) {
    fmt::print("[REG_ALLOC] State validation passed\n");
  }
}