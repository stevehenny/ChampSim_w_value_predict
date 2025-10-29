// trace_reader.cpp
// Build: g++ -std=c++17 -O2 trace_reader.cpp -o trace_reader
// Usage:
//   ./trace_reader champsim.trace
//   xz -d -c champsim.trace.xz | ./trace_reader -    (read from stdin)
//   ./trace_reader --cloudsuite champsim_cloudsuite.trace

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

constexpr std::size_t NUM_INSTR_DESTINATIONS_SPARC = 4;
constexpr std::size_t NUM_INSTR_DESTINATIONS = 2;
constexpr std::size_t NUM_INSTR_SOURCES = 4;

#pragma pack(push, 1)
struct input_instr {
  unsigned long long ip;
  unsigned char is_branch;
  unsigned char branch_taken;
  unsigned char destination_registers[NUM_INSTR_DESTINATIONS];
  unsigned char source_registers[NUM_INSTR_SOURCES];
  unsigned long long destination_memory[NUM_INSTR_DESTINATIONS];
  unsigned long long source_memory[NUM_INSTR_SOURCES];
};

struct cloudsuite_instr {
  unsigned long long ip;
  unsigned char is_branch;
  unsigned char branch_taken;
  unsigned char destination_registers[NUM_INSTR_DESTINATIONS_SPARC];
  unsigned char source_registers[NUM_INSTR_SOURCES];
  unsigned long long destination_memory[NUM_INSTR_DESTINATIONS_SPARC];
  unsigned long long source_memory[NUM_INSTR_SOURCES];
  unsigned char asid[2];
};
#pragma pack(pop)

static_assert(sizeof(input_instr) > 0, "input_instr must have size");
static_assert(sizeof(cloudsuite_instr) > 0, "cloudsuite_instr must have size");

void print_usage(const char *prog) {
  std::cerr << "Usage: " << prog << " [--cloudsuite] <trace-file-or-->\n"
            << "  Use '-' to read from stdin (useful for decompressed streaming).\n"
            << "  --cloudsuite : interpret entries as cloudsuite_instr (larger struct).\n"
            << "Example:\n"
            << "  xz -d -c mytrace.champsimtrace.xz | ./trace_reader -\n";
}

std::string to_hex(unsigned long long v) {
  std::ostringstream ss;
  ss << "0x" << std::hex << v << std::dec;
  return ss.str();
}

int main(int argc, char** argv) {
  if (argc < 2) {
    print_usage(argv[0]);
    return 1;
  }

  bool cloudsuite = false;
  std::string filename;

  // simple arg parsing
  int argi = 1;
  if (std::string(argv[argi]) == "--cloudsuite") {
    cloudsuite = true;
    ++argi;
    if (argi >= argc) { print_usage(argv[0]); return 1; }
  }
  filename = argv[argi];

  std::istream* inptr = nullptr;
  std::ifstream file;
  if (filename == "-") {
    inptr = &std::cin;
    // set binary mode for Windows (if needed)
#if defined(_WIN32)
    _setmode(_fileno(stdin), _O_BINARY);
#endif
  } else {
    file.open(filename, std::ios::binary);
    if (!file) {
      std::cerr << "Error: cannot open file '" << filename << "'\n";
      return 1;
    }
    inptr = &file;
  }

  std::istream& in = *inptr;

  const std::size_t rec_size = cloudsuite ? sizeof(cloudsuite_instr) : sizeof(input_instr);
  // buffer for raw record
  std::vector<char> buf(rec_size);

  uint64_t count = 0;
  while (in.read(buf.data(), rec_size)) {
    ++count;
    // parse
    if (!cloudsuite) {
      input_instr rec;
      std::memcpy(&rec, buf.data(), rec_size);

      // Print summary
      std::cout << std::setw(8) << count << ": PC=" << to_hex(rec.ip)
                << "  BR=" << (int)rec.is_branch
                << "  TAKEN=" << (int)rec.branch_taken;

      // destination registers
      bool any = false;
      std::cout << "  DEST_REGS=[";
      for (size_t i = 0; i < NUM_INSTR_DESTINATIONS; ++i) {
        if (rec.destination_registers[i] != 0) {
          if (any) std::cout << ",";
          std::cout << (int)rec.destination_registers[i];
          any = true;
        }
      }
      std::cout << "]";

      // source registers
      any = false;
      std::cout << "  SRC_REGS=[";
      for (size_t i = 0; i < NUM_INSTR_SOURCES; ++i) {
        if (rec.source_registers[i] != 0) {
          if (any) std::cout << ",";
          std::cout << (int)rec.source_registers[i];
          any = true;
        }
      }
      std::cout << "]";

      // memory dest
      any = false;
      std::cout << "  DEST_MEM=[";
      for (size_t i = 0; i < NUM_INSTR_DESTINATIONS; ++i) {
        if (rec.destination_memory[i] != 0) {
          if (any) std::cout << ",";
          std::cout << to_hex(rec.destination_memory[i]);
          any = true;
        }
      }
      std::cout << "]";

      // memory src
      any = false;
      std::cout << "  SRC_MEM=[";
      for (size_t i = 0; i < NUM_INSTR_SOURCES; ++i) {
        if (rec.source_memory[i] != 0) {
          if (any) std::cout << ",";
          std::cout << to_hex(rec.source_memory[i]);
          any = true;
        }
      }
      std::cout << "]\n";

    } else {
      cloudsuite_instr rec;
      std::memcpy(&rec, buf.data(), rec_size);

      std::cout << std::setw(8) << count << ": PC=" << to_hex(rec.ip)
                << "  BR=" << (int)rec.is_branch
                << "  TAKEN=" << (int)rec.branch_taken;

      // destination registers SPARC variant
      bool any = false;
      std::cout << "  DEST_REGS=[";
      for (size_t i = 0; i < NUM_INSTR_DESTINATIONS_SPARC; ++i) {
        if (rec.destination_registers[i] != 0) {
          if (any) std::cout << ",";
          std::cout << (int)rec.destination_registers[i];
          any = true;
        }
      }
      std::cout << "]";

      // source registers
      any = false;
      std::cout << "  SRC_REGS=[";
      for (size_t i = 0; i < NUM_INSTR_SOURCES; ++i) {
        if (rec.source_registers[i] != 0) {
          if (any) std::cout << ",";
          std::cout << (int)rec.source_registers[i];
          any = true;
        }
      }
      std::cout << "]";

      // mem dest
      any = false;
      std::cout << "  DEST_MEM=[";
      for (size_t i = 0; i < NUM_INSTR_DESTINATIONS_SPARC; ++i) {
        if (rec.destination_memory[i] != 0) {
          if (any) std::cout << ",";
          std::cout << to_hex(rec.destination_memory[i]);
          any = true;
        }
      }
      std::cout << "]";

      // mem src
      any = false;
      std::cout << "  SRC_MEM=[";
      for (size_t i = 0; i < NUM_INSTR_SOURCES; ++i) {
        if (rec.source_memory[i] != 0) {
          if (any) std::cout << ",";
          std::cout << to_hex(rec.source_memory[i]);
          any = true;
        }
      }
      std::cout << "]";

      // asid (two bytes)
      std::cout << "  ASID=" << ((int)rec.asid[0]) << "," << ((int)rec.asid[1]) << "\n";
    }
  }

  if (!in.eof()) {
    std::cerr << "Warning: read failed before EOF (maybe truncated record or I/O error)\n";
  } else {
    std::cerr << "Read " << count << " records.\n";
  }

  return 0;
}
