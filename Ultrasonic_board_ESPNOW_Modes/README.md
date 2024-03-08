
## Ultrasonic Board (Firmware Version V1.0.0) 
## Changelog
### Added
* Modes based on frequency of Ultrasonic data .
* Mode changes can be done using IDF Menuconfig .
* MCPWM based ultasonic pulse calaculation added for high frequency datas .
* Ultrasonic sensor port can be selected in idf menuconfig .
* Master Mac address can be entered in Menuconfig .
### Updates need to do
* Other bugfixes and features based on firmware continous testing.

#### File structure

```
.
├── CMakeLists.txt
├── components
│   └── ultrasonic
│       ├── CMakeLists.txt
│       ├── include
│       │   ├── esp_idf_lib_helpers.h
│       │   └── ultrasonic.h
│       └── ultrasonic.c
├── dependencies.lock
├── main
│   ├── CMakeLists.txt
│   ├── component.mk
│   ├── Kconfig.projbuild
│   ├── normal_main.c
│   └── speed_main.c
├── Makefile
├── README.md
├── sdkconfig
├── sdkconfig.defaults
└── sdkconfig.old

```

![Powered by](https://upload.wikimedia.org/wikipedia/commons/thumb/8/8e/Espressif_Logo.svg/2560px-Espressif_Logo.svg.png)

