# Master thesis

The code in this repository represents an implementation of the approach to automatically compile and securely distribute firmware updates over-the-air (OTA) to ESP32 based IoT devices presented in my master thesis „Automatisierte, netzwerkbasierte und sichere Bereitstellung von Firmware Updates für smarte Systeme“ written at Freie Universität Berlin, Germany.

## Directory structure and contents

| directories  | contents
|--------------|----------------------------------------------------------------
| code         | source code for the firmware
| gitlab       | Gitlab CICD configuration file .gitlab-ci.yml (might be hidden under Unix based OS), padder.sh (used in the CICD pipeline)
| misc         | Dockerfile and NVS partition table (nsv.csv)
| scripts      | scripts provided for convient building, flashing, and, creation and flashing of the NVS partition

## Notes

BUG: There is a unresolved bug which does not allow the button\_init() method to be called after any other *\_init() method.
