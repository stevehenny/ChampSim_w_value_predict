// ==========================================
// register_allocator.h - COMPLETE VERSION
// ==========================================

#ifndef REG_ALLOC_H
#define REG_ALLOC_H

#include <array>
#include <cstdint>
#include <list>
#include <optional>
#include <queue>
#include <unordered_map>
#include <vector>
#include "instruction.h"
#include <unordered_set>

struct physical_register {
  uint16_t arch_reg_index;
  uint64_t producing_instruction_id;
  bool valid; // has the producing instruction committed yet?
  bool busy;  // is this register in use anywhere in the pipeline?
};

class RegisterAllocator
{
private:
  std::array<PHYSICAL_REGISTER_ID, std::numeric_limits<uint8_t>::max() + 1> frontend_RAT, backend_RAT;
  std::queue<PHYSICAL_REGISTER_ID> free_registers;
  std::vector<physical_register> physical_register_file;

  // *** NEW: Value Prediction Support ***
  // Structure to track rename operations for potential rollback
  struct RenameCheckpoint {
    int16_t arch_reg;              // Which architectural register was renamed
    PHYSICAL_REGISTER_ID old_phys_reg;  // Physical reg it pointed to BEFORE rename
    PHYSICAL_REGISTER_ID new_phys_reg;  // Physical reg allocated BY this instruction
    uint64_t instr_id;             // For debugging
  };
  
  // History of renames for each in-flight instruction (for rollback on squash)
  std::unordered_map<uint64_t, std::vector<RenameCheckpoint>> rename_history;

public:
  RegisterAllocator(size_t num_physical_registers);
  
  // Existing interface
  PHYSICAL_REGISTER_ID rename_dest_register(int16_t reg, champsim::program_ordered<ooo_model_instr>::id_type producer_id);
  PHYSICAL_REGISTER_ID rename_src_register(int16_t reg);
  void complete_dest_register(PHYSICAL_REGISTER_ID physreg);
  void retire_dest_register(PHYSICAL_REGISTER_ID physreg);
  void free_register(PHYSICAL_REGISTER_ID physreg);
  bool isValid(PHYSICAL_REGISTER_ID physreg) const;
  bool isAllocated(PHYSICAL_REGISTER_ID archreg) const;
  unsigned long count_free_registers() const;
  int count_reg_dependencies(const ooo_model_instr& instr) const;
  void reset_frontend_RAT();
  void print_deadlock();
  void invalidate_register(PHYSICAL_REGISTER_ID physreg);
  
  // *** NEW: Value Prediction Interface ***
  
  // Get current physical register mapping for an architectural register
  PHYSICAL_REGISTER_ID get_current_mapping(int16_t arch_reg) const;
  
  // Record a rename operation for potential rollback
  void record_rename(uint64_t instr_id, int16_t arch_reg, 
                     PHYSICAL_REGISTER_ID old_phys_reg, 
                     PHYSICAL_REGISTER_ID new_phys_reg);
  
  // Undo all renames for a squashed instruction (returns phys regs to free list)
  void undo_rename(uint64_t instr_id);
  
  // Clear rename history when instruction retires (no longer need to rollback)
  void retire_rename(uint64_t instr_id);
  
  // Get the producer instruction ID for a physical register
  uint64_t get_producer(PHYSICAL_REGISTER_ID phys_reg) const;
  
  // Debug functions
  void print_rat_state() const;
  void print_rename_history() const;
  void validate_state() const;
};

#endif
