/*
 * Author: Daniel Kopta
 * Updated by: Erin Parker
 * CS 4400, University of Utah
 *
 * Simulator handout
 * A simple x86-like processor simulator.
 * Read in a binary file that encodes instructions to execute.
 * Simulate a processor by executing instructions one at a time and appropriately 
 * updating register and memory contents.
 *
 * Some code and pseudo code has been provided as a starting point.
 *
 * Completed by: STUDENT-FILL-IN
*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "instruction.h"

// Forward declarations for helper functions
unsigned int get_file_size(int file_descriptor);
unsigned int* load_file(int file_descriptor, unsigned int size);
instruction_t* decode_instructions(unsigned int* bytes, unsigned int num_instructions);
unsigned int execute_instruction(unsigned int program_counter, instruction_t* instructions, 
				 int* registers, unsigned char* memory);
void print_instructions(instruction_t* instructions, unsigned int num_instructions);
void error_exit(const char* message);

// 17 registers
#define NUM_REGS 17
// 1024-byte stack
#define STACK_SIZE 1024

int main(int argc, char** argv)
{
  // Make sure we have enough arguments
  if(argc < 2)
    error_exit("must provide an argument specifying a binary file to execute");

  // Open the binary file
  int file_descriptor = open(argv[1], O_RDONLY);
  if (file_descriptor == -1) 
    error_exit("unable to open input file");

  // Get the size of the file
  unsigned int file_size = get_file_size(file_descriptor);
  // Make sure the file size is a multiple of 4 bytes
  // since machine code instructions are 4 bytes each
  if(file_size % 4 != 0)
    error_exit("invalid input file");

  // Load the file into memory
  // We use an unsigned int array to represent the raw bytes
  // We could use any 4-byte integer type
  unsigned int* instruction_bytes = load_file(file_descriptor, file_size);
  close(file_descriptor);

  unsigned int num_instructions = file_size / 4;


  /****************************************/
  /**** Begin code to modify/implement ****/
  /****************************************/

  // Allocate and decode instructions (left for you to fill in)
  instruction_t* instructions = decode_instructions(instruction_bytes, num_instructions);


  // Optionally print the decoded instructions for debugging
  // Will not work until you implement decode_instructions
  // Do not call this function in your submitted final version
  // print_instructions(instructions, num_instructions);  

  // Allocate and initialize registers
  int* registers = (int*)malloc(sizeof(int) * NUM_REGS);
  initialize_registers(registers);

  // Stack memory is byte-addressed, so it must be a 1-byte type
  unsigned char* memory = (unsigned char*)malloc(sizeof(unsigned char) * STACK_SIZE);
  initialize_memory(memory);

  // Run the simulation
  unsigned int program_counter = 0;

  // program_counter is a byte address, so we must multiply num_instructions by 4 
  // to get the address past the last instruction
  while(program_counter != num_instructions * 4)
  {
    program_counter = execute_instruction(program_counter, instructions, registers, memory);
  }
  
  return 0;
}

/*
Bits:       31 - 27     26 - 22 	21 - 17 	16	        15 - 0
Meaning:  	opcode	    reg1	    reg2	    unused	    immediate
*/

/*
 * Decodes the array of raw instruction bytes into an array of instruction_t
 * Each raw instruction is encoded as a 4-byte unsigned int
*/
instruction_t* decode_instructions(unsigned int* bytes, unsigned int num_instructions)
{
    instruction_t* retval = (instruction_t*)malloc(sizeof(instruction_t) * num_instructions);

  int i;
  for (i = 0; i < num_instructions; i++) {
      retval[i].opcode = (bytes[i] >> 27) & 0x1F;
      retval[i].first_register = (bytes[i] >> 22) & 0x1F;
      retval[i].second_register = (bytes[i] >> 17) & 0x1F;
	  retval[i].immediate = (short)(bytes[i] & 0xFFFF); // No shift needed, just mask the lower 16 bits
  }
    
  return retval;
}

/*
 * Executes a single instruction and returns the next program counter
*/
unsigned int execute_instruction(unsigned int program_counter, instruction_t* instructions, int* registers, unsigned char* memory)
{

  // program_counter is a byte address, but instructions are 4 bytes each
  // divide by 4 to get the index into the instructions array
  instruction_t instr = instructions[program_counter / 4];

    // Debugging output
    //printf("PC=%u opcode=%d reg1=%d reg2=%d imm=%u ESP=%d\n", program_counter, instr.opcode, instr.first_register, instr.second_register, instr.immediate, registers[6]);

    switch(instr.opcode)
    {
    case subl: // 0
        registers[instr.first_register] = registers[instr.first_register] - instr.immediate;
    break;
    case addl_reg_reg: // 1
        registers[instr.second_register] = registers[instr.second_register] + registers[instr.first_register];
    break;
    case addl_imm_reg: // 2
        registers[instr.first_register] = registers[instr.first_register] + instr.immediate;
    break;
    case imull: // 3    
        registers[instr.second_register] = registers[instr.first_register] * registers[instr.second_register];
    break;
    case shrl: // 4
        registers[instr.first_register] = (unsigned) (registers[instr.first_register]) >> 1;
    break;
    case movl_reg_reg: // 5
        registers[instr.second_register] = registers[instr.first_register];
    break;
	case movl_deref_reg: // 6
        registers[instr.second_register] = *(int*)(memory + registers[instr.first_register] + instr.immediate);
    break;
	case movl_reg_deref: // 7
        *(int*)(memory + registers[instr.second_register] + instr.immediate) = registers[instr.first_register];
    break;
	case movl_imm_reg: // 8
        registers[instr.first_register] = (signed) instr.immediate;
    break;
    case cmpl: // 9
        int op1 = registers[instr.first_register];
        int op2 = registers[instr.second_register];

        // cmpl %reg1, %reg2 performs reg2 - reg1
        int result = op2 - op1;

        // Clear flags
        registers[16] = 0;

        // Zero Flag (ZF) - bit 6
        if (result == 0) {
            registers[16] |= 64;
        }

        // Sign Flag (SF) - bit 7
        if (result < 0) {
            registers[16] |= 128;
        }

        // Carry Flag (CF) - bit 0
        // For subtraction, borrow occurs when op2 < op1 unsigned
        if ((unsigned int)op2 < (unsigned int)op1) {
            registers[16] |= 1;
        }

        // Overflow Flag (OF) - bit 11
        if (((op2 < 0) != (op1 < 0)) &&
            ((result < 0) != (op2 < 0))) {
            registers[16] |= 2048;
        }
    break;
	case je: // 10
		return (registers[16] & 64) ? (program_counter + 4) + instr.immediate : program_counter + 4;
    break;
	case jl: // 11
        return ((registers[16] & 128) ^ (registers[16] & 2048)) ? (program_counter + 4) + instr.immediate : program_counter + 4;
    break;
	case jle: // 12
        return (((registers[16] & 128) ^ (registers[16] & 2048)) | (registers[16] & 64)) ? (program_counter + 4) + instr.immediate : program_counter + 4;
    break;
	case jge: // 13
        return (!((registers[16] & 128) ^ (registers[16] & 2048))) ? (program_counter + 4) + instr.immediate : program_counter + 4;
    break;
	case jbe: // 14
        return ((registers[16] & 64) | (registers[16] & 1)) ? (program_counter + 4) + instr.immediate : program_counter + 4;
    break;
	case jmp: // 15
        return (program_counter + 4) + instr.immediate;
    break;
	case call: // 16
        // Push the return address onto the stack
        registers[6] -= 4; // Decrement %esp (register 6) by 4
        *(int*)(memory + registers[6]) = program_counter + 4; // Store return address
        return (program_counter + 4) + instr.immediate; // Jump to the function
    break;
    case ret:
		// Pop the return address from the stack and jump to it
		if (registers[6] == STACK_SIZE) { // If the stack pointer is at the top of the stack, it means there are no return addresses left to pop
            exit(0);
        }

        program_counter = *(int*)(memory + registers[6]);
        registers[6] += 4;
        return program_counter;
    break;
	case pushl: // 18
        registers[6] -= 4; // Decrement %esp (register 6) by 4
        *(int*)(memory + registers[6]) = registers[instr.first_register]; // Store value onto stack
    break;
	case popl: // 19
        registers[instr.first_register] = *(int*)(memory + registers[6]); // Load value from stack
        registers[6] += 4; // Increment %esp (register 6) by 4
    break;
	case printr: // 20
        printf("%d (0x%x)\n", registers[instr.first_register], registers[instr.first_register]);
    break;
	case readr: // 21
        scanf("%d", &(registers[instr.first_register]));
    break;
  }
  // If the instruction was not a jump, call, or ret, increment the program counter by 4 to move to the next instruction
  return program_counter + 4;
}

// Initializes the registers to 0, except for %esp which is initialized to point to the top of the stack
void initialize_registers(int* registers) {
    for (int i = 0; i < NUM_REGS; i++) {
        registers[i] = 0;
    }
	registers[6] = STACK_SIZE; // Initialize %esp to point to the top of the stack, Register ID = 6;
}

// Initializes the stack memory to 0
void initialize_memory(unsigned char* memory) {
    for (int i = 0; i < STACK_SIZE; i++) {
        memory[i] = 0;
    }
}


/*********************************************/
/****  DO NOT MODIFY THE FUNCTIONS BELOW  ****/
/*********************************************/

/*
 * Returns the file size in bytes of the file referred to by the given descriptor
*/
unsigned int get_file_size(int file_descriptor)
{
  struct stat file_stat;
  fstat(file_descriptor, &file_stat);
  return file_stat.st_size;
}

/*
 * Loads the raw bytes of a file into an array of 4-byte units
*/
unsigned int* load_file(int file_descriptor, unsigned int size)
{
  unsigned int* raw_instruction_bytes = (unsigned int*)malloc(size);
  if(raw_instruction_bytes == NULL)
    error_exit("unable to allocate memory for instruction bytes (something went really wrong)");

  int num_read = read(file_descriptor, raw_instruction_bytes, size);

  if(num_read != size)
    error_exit("unable to read file (something went really wrong)");

  return raw_instruction_bytes;
}

/*
 * Prints the opcode, register IDs, and immediate of every instruction, 
 * assuming they have been decoded into the instructions array
*/
void print_instructions(instruction_t* instructions, unsigned int num_instructions)
{
  printf("instructions: \n");
  unsigned int i;
  for(i = 0; i < num_instructions; i++)
  {
    printf("op: %d, reg1: %d, reg2: %d, imm: %d\n", 
	   instructions[i].opcode,
	   instructions[i].first_register,
	   instructions[i].second_register,
	   instructions[i].immediate);
  }
  printf("--------------\n");
}

/*
 * Prints an error and then exits the program with status 1
*/
void error_exit(const char* message)
{
  printf("Error: %s\n", message);
  exit(1);
}
