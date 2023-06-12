#include "libusb.h"
#include "portmidi.h"
#include "porttime.h"
#include <iomanip>
#include <iostream>
#include <stdio.h>

#define OUTPUT_BUFFER_SIZE 0
#define INPUT_BUFFER_SIZE 10
#define DEVICE_INFO NULL
#define DRIVER_INFO NULL
#define TIME_PROC ((PmTimeProcPtr)Pt_Time)
#define TIME_INFO NULL
#define TIME_START                                                             \
  Pt_Start(1, 0, 0) /* timer started w/millisecond accuracy                    \
                     */

inline uint16_t Reverse16(uint16_t value) {
  return (((value & 0x00FF) << 8) | ((value & 0xFF00) >> 8));
}

inline uint32_t Reverse32(uint32_t value) {
  return (((value & 0x000000FF) << 24) | ((value & 0x0000FF00) << 8) |
          ((value & 0x00FF0000) >> 8) | ((value & 0xFF000000) >> 24));
}

int main() {
  int result = 0;

  libusb_init_context(/*ctx=*/NULL, /*options=*/NULL, /*num_options=*/0);

  libusb_device **devs;
  ssize_t cnt = libusb_get_device_list(NULL, &devs);
  if (cnt < 0) {
    std::cout << "No libusb" << std::endl;
    libusb_exit(NULL);
    return 1;
  }

  bool found = false;
  for (ssize_t i = 0; devs[i]; i++) {
    struct libusb_device_descriptor desc;
    libusb_get_device_descriptor(devs[i], &desc);
    if (desc.idVendor == 0x0582 && desc.idProduct == 0x012A) {
      found = true;
      libusb_device_handle *handle;
      libusb_open(devs[i], &handle);

      if (!handle) {
        std::cout << "No handle" << std::endl;
        libusb_free_device_list(devs, 1);
        libusb_exit(NULL);
      }

      result = libusb_claim_interface(handle, 0);
      if (result != LIBUSB_SUCCESS) {
        std::cout << "Error claiming interface: " << libusb_error_name(result)
                  << std::endl;
      }

      // result = libusb_set_interface_alt_setting(handle, 0, 0);
      // if (result != LIBUSB_SUCCESS) {
      //   std::cout << "Error setting interface: " << libusb_error_name(result)
      //             << std::endl;
      // }

      Pm_Initialize();

      // allocate some space we will alias with open-ended PmDriverInfo:
      static char dimem[sizeof(PmSysDepInfo) + sizeof(void *) * 2];
      PmSysDepInfo *sysdepinfo = (PmSysDepInfo *)dimem;
      // build the driver info structure:
      sysdepinfo->structVersion = PM_SYSDEPINFO_VERS;
      sysdepinfo->length = 1;
      sysdepinfo->properties[0].key = pmKeyCoreMidiManufacturer;
      char *strRoland = "Roland";
      sysdepinfo->properties[0].value = strRoland;

      int in_id = Pm_CreateVirtualInput("UM-ONE", NULL, sysdepinfo);
      int out_id = Pm_CreateVirtualOutput("UM-ONE", NULL, sysdepinfo);

      PmEvent buffer[1];
      PmStream *out;
      PmStream *in;
      Pm_OpenInput(&in, in_id, NULL, 0, NULL, NULL);
      Pm_OpenOutput(&out, out_id, NULL, OUTPUT_BUFFER_SIZE, TIME_PROC,
                    TIME_INFO, 0);
      printf("Created/Opened input %d and output %d\n", in_id, out_id);
      Pm_SetFilter(in, PM_FILT_ACTIVE | PM_FILT_CLOCK | PM_FILT_SYSEX);

      /* empty the buffer after setting filter, just in case anything got
       * through */
      while (Pm_Poll(in)) {
        Pm_Read(in, buffer, 1);
      }

      while (true) {
        // Read from virtual port
        PmError status = Pm_Poll(in);
        if (status == TRUE) {
          int length = Pm_Read(in, buffer, 1);
          if (length > 0) {
            // printf("Got message: time %ld, %2lx %2lx %2lx\n",
            //        (long)buffer[0].timestamp,
            //        (long)Pm_MessageStatus(buffer[0].message),
            //        (long)Pm_MessageData1(buffer[0].message),
            //        (long)Pm_MessageData2(buffer[0].message));

            int actualLength = 0;
            // uint32_t reversed = Reverse32(buffer[0].message) >> 8;
            // uint32_t reversed = buffer[0].message;
            // printf("%x\n", reversed);
            unsigned char sendBuffer[] = {
                ((Pm_MessageStatus(buffer[0].message) & 0xF)) |
                    ((Pm_MessageStatus(buffer[0].message) & 0xF0) >> 4),
                Pm_MessageStatus(buffer[0].message),
                Pm_MessageData1(buffer[0].message),
                Pm_MessageData2(buffer[0].message)};
            // for (size_t i = 0; i < sizeof(sendBuffer); i++) {
            //   printf("%02X", sendBuffer[i]);
            // }
            // std::cout << std::endl;
            result =
                libusb_bulk_transfer(handle, 0x02, sendBuffer,
                                     sizeof(sendBuffer), &actualLength, 100);
            // std::cout << "Result: " << libusb_error_name(result) <<
            // std::endl; std::cout << "Actual length: " << actualLength <<
            // std::endl;
          } else {
            assert(0);
          }
        }

        // Read from USB
        int actualLength = 0;
        unsigned char readBuffer[64] = {0};
        result = libusb_bulk_transfer(handle, 0x81, readBuffer,
                                      sizeof(readBuffer), &actualLength, 1);
        // std::cout << "Read: " << libusb_error_name(result) << std::endl;
        if (result == LIBUSB_SUCCESS && actualLength > 0) {
          // for (size_t i = 0; i < actualLength; i++) {
          //   printf("%02X", readBuffer[i]);
          // }
          // std::cout << std::endl;

          for (size_t i = 0; i < actualLength / 4; i++) {
            PmEvent event;
            event.timestamp = Pt_Time();
            event.message = (readBuffer[1] << 0) | (readBuffer[2] << 8) |
                            (readBuffer[3] << 16);
            printf("%08X\n", event.message);
            Pm_Write(out, &event, 1);
          }
        }

        // if (actualLength == sizeof(PmMessage)) {
        //   PmEvent event;
        //   event.timestamp = Pt_Time();
        //   event.message = readBuffer;
        //   Pm_Write(out, &event, 1);
        // }
      }

      // Pt_Sleep(10000);

      Pm_Terminate();

      libusb_release_interface(handle, 0);
      libusb_close(handle);
    }
  }

  if (!found) {
    std::cout << "No device" << std::endl;
  }

  libusb_free_device_list(devs, 1);

  libusb_exit(NULL);
  return 0;
}
