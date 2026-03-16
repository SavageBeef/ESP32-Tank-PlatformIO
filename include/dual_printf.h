#ifndef DUAL_PRINTF_H
#define DUAL_PRINTF_H

#include <Arduino.h> // For Serial object
#include <stdarg.h>  // For va_list, va_start, va_end
#include "WebSerial.h" // Ensure the original library is included

// Define a simple alias for the global WebSerial object's printf capability
// (WebSerial_printf is defined in WebSerial_printf.h)
// WebSerial.print is the fallback.
#ifndef WebSerial_printf
    #define WebSerial_printf WebSerial.print
#endif

/**
 * @brief Prints a formatted message to both the standard Serial console and WebSerial.
 * * @param format The standard printf format string.
 * @param ... The variable arguments to be formatted.
 */
void dualPrintf(const char* format, ...) {
  // Define a reasonable fixed buffer size. Must be large enough for your longest messages.
  const int bufferSize = 128; 
  char buffer[bufferSize];

  // 1. Handle variable arguments
  va_list args;
  va_start(args, format);

  // 2. Format the string safely into the buffer (Reuses the key logic from before)
  int written = vsnprintf(buffer, bufferSize, format, args);

  // 3. Clean up
  va_end(args);

  // 4. Output to both destinations
  if (written > 0) {
    // Destination 1: Standard Serial
    Serial.print(buffer);

    // Destination 2: WebSerial Lite
    WebSerial.print(buffer);
  }
}

#endif // DUAL_PRINTF_H