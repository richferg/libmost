#ifndef MOST_ERROR_ENUM_H
#define MOST_ERROR_ENUM_H

// MOST High Protocol – Standard Result Codes
typedef enum {
    MOST_OK                     = 0x00, // Operation successful

    MOST_ERR_UNKNOWN_FUNCTION   = 0x01, // FktID not implemented
    MOST_ERR_UNKNOWN_OPTYPE     = 0x02, // Unsupported OpType (Read/Write/Method)
    MOST_ERR_PARAMETER_ERROR    = 0x03, // Parameter invalid or wrong count
    MOST_ERR_FORMAT_ERROR       = 0x04, // Payload formatting error
    MOST_ERR_UNKNOWN_PROPERTY   = 0x05, // No such Property
    MOST_ERR_RANGE_ERROR        = 0x06, // Parameter out of valid range
    MOST_ERR_DEVICE_NOT_READY   = 0x07, // Device is busy or not in correct state
    MOST_ERR_READ_ONLY          = 0x08, // Write attempted on ReadOnly property
    MOST_ERR_WRITE_ONLY         = 0x09, // Read attempted on WriteOnly property
    MOST_ERR_DIRECTORY_FULL     = 0x0A, // No more directory entries free
    MOST_ERR_UNKNOWN_ENUM_VALUE = 0x0B, // Enum item does not exist
    MOST_ERR_BUFFER_OVERFLOW    = 0x0C, // Payload too large
    MOST_ERR_BUFFER_UNDERFLOW   = 0x0D, // Payload too small
    MOST_ERR_BUSY               = 0x0E, // Device busy (temporary condition)
    MOST_ERR_NOT_ACCESSIBLE     = 0x0F, // Function blocked or unavailable
    MOST_ERR_NOT_AVAILABLE      = 0x10, // Hardware or feature absent
    MOST_ERR_PROTECTED          = 0x11, // Protected or locked Feature

    // Extended error codes used by some OEM FCATs
    MOST_ERR_TIMEOUT            = 0x12, // Timeout waiting for resource
    MOST_ERR_COLLISION          = 0x13, // Collision in resource access
    MOST_ERR_CHECKSUM           = 0x14, // CRC/Checksum failure
    MOST_ERR_INTERNAL           = 0x15, // Internal device error

    MOST_ERR_UNKNOWN            = 0xFF  // Catch-all for invalid/undefined results
} most_error_t;

#endif // MOST_ERROR_ENUM_H
