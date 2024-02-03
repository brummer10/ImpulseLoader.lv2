# ImpulseLoader

This is a simple, mono, IR-File loader/convolution LV2 plug. 

![ImpulseLoader](https://raw.githubusercontent.com/brummer10/ImpulseLoader.lv2/master/ImpulseLoader.png)

## Supported File formats been:

- WAF
- AIFF
- WAVEFORMATEX
- CAF

IR-Files could be loaded via the integrated File Browser, or, when supported by the host, via drag and drop.

If the IR-File have more then 1 channel, only the first channel will be used.

IR-Files will be resampled on the fly to match the session Sample Rate.

## Dependencies

- libcairo2-dev
- libx11-dev
- lv2-dev

## Build

- git submodule init
- git submodule update
- make
- make install # will install into ~/.lv2 ... AND/OR....
- sudo make install # will install into /usr/lib/lv2

