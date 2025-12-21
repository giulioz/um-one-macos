#include <iomanip>
#include <iostream>
#include <stdio.h>
#include <vector>
#include <libusb-1.0/libusb.h>
#include <portmidi.h>
#include <porttime.h>

#define OUTPUT_BUFFER_SIZE 0
#define INPUT_BUFFER_SIZE 10
#define TIME_PROC ((PmTimeProcPtr)Pt_Time)
#define TIME_INFO NULL

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
      const char *strRoland = "Roland";
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
      Pm_SetFilter(in, PM_FILT_ACTIVE | PM_FILT_CLOCK);

      // Empty the buffer, just in case anything got through
      while (Pm_Poll(in)) {
        Pm_Read(in, buffer, 1);
      }

      // Arbitrarily big buffer
      const int sendBufferNEvents = 1024;
      unsigned char sendBuffer[sendBufferNEvents * 4] = {0};

      bool sysexSendMode = false;
      std::vector<unsigned char> sysexOutBytes;
      std::vector<unsigned char> sysexInBytes;

      auto flushSysexToUSB = [&]() {
        std::vector<unsigned char> sysexSendBytes;
        for (size_t i = 0; i < sysexOutBytes.size(); i += 3) {
          if (i == sysexOutBytes.size() - 3) {
            sysexSendBytes.push_back(0x07);
            sysexSendBytes.push_back(sysexOutBytes[i + 0]);
            sysexSendBytes.push_back(sysexOutBytes[i + 1]);
            sysexSendBytes.push_back(sysexOutBytes[i + 2]);
          } else if (i == sysexOutBytes.size() - 2) {
            sysexSendBytes.push_back(0x06);
            sysexSendBytes.push_back(sysexOutBytes[i + 0]);
            sysexSendBytes.push_back(sysexOutBytes[i + 1]);
          } else if (i == sysexOutBytes.size() - 1) {
            sysexSendBytes.push_back(0x05);
            sysexSendBytes.push_back(sysexOutBytes[i + 0]);
          } else {
            sysexSendBytes.push_back(0x04);
            sysexSendBytes.push_back(sysexOutBytes[i + 0]);
            sysexSendBytes.push_back(sysexOutBytes[i + 1]);
            sysexSendBytes.push_back(sysexOutBytes[i + 2]);
          }
        }

        int actualLength = 0;
        libusb_bulk_transfer(handle, 0x02, &sysexSendBytes[0],
                             sysexSendBytes.size(), &actualLength, 1);
        sysexOutBytes.clear();

        // printf("Sending sysex: ");
        // for (size_t i = 0; i < sysexSendBytes.size(); i++) {
        //   printf("%02x ", sysexSendBytes[i]);
        // }
        // printf("\n");
      };

      auto flushSysexToHost = [&]() {
        if (sysexInBytes.size() < 4) {
          sysexInBytes.push_back(0);
          sysexInBytes.push_back(0);
          sysexInBytes.push_back(0);
          sysexInBytes.push_back(0);
        }

        PmEvent event;
        event.timestamp = Pt_Time();
        event.message = (sysexInBytes[0] << 0) | (sysexInBytes[1] << 8) |
                        (sysexInBytes[2] << 16) | (sysexInBytes[3] << 24);
        Pm_Write(out, &event, 1);

        sysexInBytes.clear();
      };

      auto pushSysexToUSB = [&](unsigned char byte) {
        if (sysexOutBytes.size() == 48) {
          flushSysexToUSB();
        }
        sysexOutBytes.push_back(byte);
      };

      auto endSysexToUSB = [&](unsigned char byte) {
        if (sysexOutBytes.size() == 48) {
          flushSysexToUSB();
        }
        sysexOutBytes.push_back(byte);
        flushSysexToUSB();
      };

      while (true) {
        // Read from virtual port
        int length = Pm_Read(in, buffer, sendBufferNEvents);
        if (length == pmBufferOverflow || length > sendBufferNEvents) {
          std::cout << "Buffer overflow!" << std::endl;
        }

        if (length > 0) {
          for (size_t i = 0; i < length; i++) {
            // printf("Got message: time %ld, %02x %02x %02x %02x\n",
            //        (long)buffer[i].timestamp,
            //        Pm_MessageStatus(buffer[i].message),
            //        Pm_MessageData1(buffer[i].message),
            //        Pm_MessageData2(buffer[i].message),
            //        (buffer[i].message >> 24) & 0xFF);

            if (!sysexSendMode && Pm_MessageStatus(buffer[i].message) == 0xF0) {
              sysexSendMode = true;
            }

            if (sysexSendMode) {
              auto b1 = (buffer[i].message >> 0) & 0xFF;
              if (b1 != 0xF0 && b1 & 0b10000000) {
                endSysexToUSB(b1);
                sysexSendMode = false;
              } else if (sysexSendMode) {
                pushSysexToUSB(b1);
              }

              auto b2 = (buffer[i].message >> 8) & 0xFF;
              if (b2 != 0xF0 && b2 & 0b10000000) {
                endSysexToUSB(b2);
                sysexSendMode = false;
              } else if (sysexSendMode) {
                pushSysexToUSB(b2);
              }

              auto b3 = (buffer[i].message >> 16) & 0xFF;
              if (b3 != 0xF0 && b3 & 0b10000000) {
                endSysexToUSB(b3);
                sysexSendMode = false;
              } else if (sysexSendMode) {
                pushSysexToUSB(b3);
              }

              auto b4 = (buffer[i].message >> 24) & 0xFF;
              if (b4 != 0xF0 && b4 & 0b10000000) {
                endSysexToUSB(b4);
                sysexSendMode = false;
              } else if (sysexSendMode) {
                pushSysexToUSB(b4);
              }
            } else {
              sendBuffer[i * 4 + 0] =
                  (Pm_MessageStatus(buffer[i].message) & 0xF0) >> 4;
              sendBuffer[i * 4 + 1] = Pm_MessageStatus(buffer[i].message);
              sendBuffer[i * 4 + 2] = Pm_MessageData1(buffer[i].message);
              sendBuffer[i * 4 + 3] = Pm_MessageData2(buffer[i].message);
            }
          }

          int actualLength = 0;
          result = libusb_bulk_transfer(handle, 0x02, sendBuffer, length * 4,
                                        &actualLength, 1);
          // std::cout << "Result: " << libusb_error_name(result) <<
          // std::endl; std::cout << "Actual length: " << actualLength <<
          // std::endl;
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
            auto controlByte = readBuffer[i * 4];
            auto b1 = readBuffer[i * 4 + 1];
            auto b2 = readBuffer[i * 4 + 2];
            auto b3 = readBuffer[i * 4 + 3];
            // printf("%02x %02x %02x %02x\n", controlByte, b1, b2, b3);

            if (controlByte == 0x04) {
              sysexInBytes.push_back(b1);
              if (sysexInBytes.size() == 4)
                flushSysexToHost();
              sysexInBytes.push_back(b2);
              if (sysexInBytes.size() == 4)
                flushSysexToHost();
              sysexInBytes.push_back(b3);
              if (sysexInBytes.size() == 4)
                flushSysexToHost();
            } else if (controlByte >= 0x05 || controlByte <= 0x07) {
              sysexInBytes.push_back(b1);
              if (sysexInBytes.size() == 4)
                flushSysexToHost();
              sysexInBytes.push_back(b2);
              if (sysexInBytes.size() == 4)
                flushSysexToHost();
              sysexInBytes.push_back(b3);
              flushSysexToHost();
            } else {
              PmEvent event;
              event.timestamp = Pt_Time();
              event.message = (readBuffer[i * 4 + 1] << 0) |
                              (readBuffer[i * 4 + 2] << 8) |
                              (readBuffer[i * 4 + 3] << 16);
              Pm_Write(out, &event, 1);
            }
          }
        }
      }

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
