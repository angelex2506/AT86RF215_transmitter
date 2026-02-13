# README

Repository on streaming and transmission of IQ samples via AT86RF215 chip and a Zedboard. 

## Motivation

There are several interesting projects that leverage the AT86RF215 chip and its IQ mode
to build a software-defined radio platform.  All of these projects propose a dedicated solution with their own board that integrates a dedicated FPGA and/or CPU.  The main idea of this repository is to propose solutions for using the AT86RF215 with existing FPGA development kits such as the Zynq-7000 Zedboard.
On this occasion, an IQ sample transmitter will be designed, from the creation of the architecture in the PL to the configuration of PS registers.


## Read the documentation

Next, we assume that you have the Pip software for Python3.
If this is not the case in Debian, you can install it as follows:

```
sudo apt install python3-pip
```

Install the dependencies:

```
pip install -r docs/requirement.txt
```

Sphinx, a tool that facilitates the creation of documentation, will be installed.
To generate and read the documentation:

To generate and read the documentation:

```
make -C docs html
firefox docs/build/html/index.html
```

## List of pending tasks
[] Correctly configure the AT86RF215 registers in time with the synchronization of the external IQ samples. This will allow them to be transmitted by RF.

[] Transmit QCSP frames in real time by accessing a memory or sending the samples directly from a file in MATLAB via UART.
