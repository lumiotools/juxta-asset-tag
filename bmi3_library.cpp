// BMI3 Base Library Wrapper
// Includes bmi3.c from libs_source folder
// Arduino IDE will compile this .cpp file, which includes the .c file

extern "C" {
  // Include from libs_source subfolder (headers are in same folder)
  #include "libs_source/bmi3.c"
}

