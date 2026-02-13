import time
import numpy as np
import matplotlib.pyplot as plt


def bindigits(n, bits):
    s = bin(n & int("1"*bits, 2))[2:]
    return ("{0:0>%s}" % (bits)).format(s)

def main():
    ## Generate waveform: a single tone
    Fs = 2e6 # Sampling frequency
    Ts = 1/Fs
    f = 125e3 # tone frequency in Hz
    N = 64 # number of samples to generate
    t = Ts*np.arange(N)

    real = np.cos(2*np.pi*f*t)*1
    imag = 0
    tx_samples = real + 1j*imag # complex float

    # Figure: signal in time domain 
    plt.title('Waveform I and Q representation')
    plt.plot(t, tx_samples.real[:],label='I')
    plt.plot(t, tx_samples.imag[:],label='Q')
    plt.xlabel("Time [s]")
    plt.ylabel("Amplitude []")
    plt.legend(framealpha=1,frameon=True);
    plt.show()

    # Figure: signal in frequency domain
    PSD = (np.abs(np.fft.fft(tx_samples))/N)**2
    PSD_log = 10.0*np.log10(PSD)
    PSD_shifted = np.fft.fftshift(PSD_log)

    f = np.arange(Fs/-2.0, Fs/2.0, Fs/N) # start, stop, step

    plt.plot(f, PSD_shifted)
    plt.title('PSD of single tone waveform')
    plt.xlabel("Frequency [Hz]")
    plt.ylabel("Magnitude [dB]")
    plt.grid(True)
    plt.show()

    # Prepare buffer of samples for Digital to Analog converter
    data = np.empty([2*N,],dtype=np.int16)
    ADC_SAMPLE_BITS = 14
    data[0::2] = (2**(ADC_SAMPLE_BITS-1)-1)* tx_samples.real
    data[1::2] = (2**(ADC_SAMPLE_BITS-1)-1)* tx_samples.imag

    # Convert to binary word with word width equal to 14 bits Word format
    # datasheet AT86RF215 page 24:"The actual baseband signal data is contained
    # in sub-fields I_DATA[13:1] and Q_DATA[13:1], each interpreted as 13-bit
    # 2’s complement signed values with {I,Q}_DATA[13] being the sign bit and
    # {I,Q}_DATA[1] being the least significant bit."
    # And write data to a file to be used in VHDL ram init
    fname = open("iq-samples-for-vhdl-ram.txt", "w")
    for val in np.nditer(data):
        fname.write(bindigits(val,ADC_SAMPLE_BITS))
        fname.write('\n')
    fname.close()

if __name__ == '__main__':
    main()
