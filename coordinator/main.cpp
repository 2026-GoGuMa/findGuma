#include "L2_FSMmain.h"
#include "L3_FSMmain.h"
#include "mbed.h"
#include "protocol_parameters.h"
#include "string.h"
#include "../ascii_art.h"

// serial port interface
Serial pc(USBTX, USBRX);

// GLOBAL variables (DO NOT TOUCH!) ------------------------------------------

// source ID
uint8_t input_thisId = L3_COORDINATOR_ID;

// FSM operation implementation ------------------------------------------------
int main(void) {
  print_startup_banner(&pc);
  log_box(&pc, "COORDINATOR | ID: %d | Searching for nearby traders...", input_thisId);

  // initialize lower layer stacks
  L2_initFSM(input_thisId);
  L3_initFSM();

  while (1) {
    L2_FSMrun();
    L3_FSMrun();
  }
}