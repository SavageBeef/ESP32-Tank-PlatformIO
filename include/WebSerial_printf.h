#ifndef WEBSERIAL_PRINTF_H
#define WEBSERIAL_PRINTF_H

#include <stdarg.h> // Required for va_list, va_start, va_end
#include <WebSerial.h> // Required to print to WebSerial

void webSerialPrintf(const char* format, ...) {
  // Define a buffer size. Adjust this based on the typical length of your messages.
  // 128 characters is usually a good starting point for simple messages.
  const int bufferSize = 128;
  char buffer[bufferSize];

  // 1. Initialize the variable argument list
  va_list args;
  va_start(args, format);

  // 2. Format the string into the buffer
  // vsnprintf is the safe way to format a variable argument list into a buffer,
  // preventing buffer overflows by respecting bufferSize.
  int written = vsnprintf(buffer, bufferSize, format, args);

  // 3. Clean up the variable argument list
  va_end(args);

  // 4. Print the formatted string to WebSerial
  // Check if writing was successful and not truncated (written < bufferSize)
  if (written > 0) {
    WebSerial.print(buffer);
  }
}

// ** THE MACRO TRICK **
// This macro redefines the WebSerial object's printf call to point 
// to our custom implementation function. This allows you to use 
// WebSerial.printf() without changing the original library files.
#define WebSerial_printf webSerialPrintf

#endif // WEBSERIAL_PRINTF_H