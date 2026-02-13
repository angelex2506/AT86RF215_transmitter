// Transmitir IQ (fo=125kHz, fs=2MHz) sobre fc=868MHz usando PS->DMA->PL->LVDS->AT86RF215

#include <stdint.h>
#include "platform.h"
#include "xil_printf.h"
#include "xstatus.h"
#include "xparameters.h"
#include "sleep.h"
#include "xspi.h"
#include "xaxidma.h"
#include "xil_cache.h"

/* =========================================================
 * ===============  USER CONFIG (BASEADDR)  =================
 * ========================================================= */
#define SPI_BASEADDR    XPAR_XSPI_0_BASEADDR
#define SPI_SS_MASK     0x01U

#define DMA_BASEADDR    XPAR_XAXIDMA_0_BASEADDR

/* Tamaño total del buffer DMA (words de 32 bits) */
#define IQ_WORDS_TX     256u
#define WORD_BYTES      4u
#define BYTES_TX        (IQ_WORDS_TX * WORD_BYTES)

/* --- Preamble/Trailer para I/Q Radio Mode --- */
#define PRE_ZERO_WORDS   32u  /* 32 words de 0 antes del primer IQ word */
#define POST_ZERO_WORDS  16u  /* 16 words de 0 al final para caída del PA */

/* =========================================================
 * ===============  AT86RF215 SPI COMMANDS  =================
 * ========================================================= */
#define RF215_CMD_READ(addr14)   ((uint16_t)((addr14) & 0x3FFFU))
#define RF215_CMD_WRITE(addr14)  ((uint16_t)(0x8000U | ((addr14) & 0x3FFFU)))

/* Global registers */
#define RF_PN_ADDR        0x000D
#define RF_IQIFC0_ADDR    0x000A
#define RF_IQIFC1_ADDR    0x000B
#define RF_IQIFC2_ADDR    0x000C
#define IQIFC2_SYNC_MASK  0x80u  /* status bit (RO) */

/* IQIFC1: CHPM bits [6:4] */
#define IQIFC1_CHPM_SHIFT 4u
#define IQIFC1_CHPM_MASK  (0x7u << IQIFC1_CHPM_SHIFT)
#define IQIFC1_CHPM_IQMODE 0x1u

/* IQIFC0: EEC bit0 */
#define IQIFC0_EEC_MASK   0x01u

/* RF09 register map */
#define RF09_CMD          0x0103
#define RF09_CS           0x0104
#define RF09_CCF0L        0x0105
#define RF09_CCF0H        0x0106
#define RF09_CNL          0x0107
#define RF09_CNM          0x0108

#define RF09_TXCUTC       0x0112
#define RF09_TXDFE        0x0113
#define RF09_PAC          0x0114

#define CMD_TRXOFF        0x02
#define CMD_TXPREP        0x03
#define CMD_TX            0x04

/* =========================================================
 * ====================  GLOBALS  ==========================
 * ========================================================= */
static XSpi    Spi;
static XAxiDma AxiDma;

static u32 tx_buf[IQ_WORDS_TX] __attribute__ ((aligned(64)));

/* =========================================================
 * IQ samples (I14,Q14,...) extraídos de tus binarios
 * fo=125kHz, fs=2MHz
 * ========================================================= */
static const u16 IQ_WORDS_14B[] = {
    0x1FFF, 0x0000,
    0x1D8F, 0x0000,
    0x169F, 0x0000,
    0x0C3E, 0x0000,
    0x0000, 0x0000,
    0x33C2, 0x0000,
    0x2961, 0x0000,
    0x2271, 0x0000,
    0x2001, 0x0000,
    0x2271, 0x0000,
    0x2961, 0x0000,
    0x33C2, 0x0000,
    0x0000, 0x0000,
    0x0C3E, 0x0000,
    0x169F, 0x0000,
    0x1D8F, 0x0000,
    0x1FFF, 0x0000,
    0x1D8F, 0x0000,
    0x169F, 0x0000,
    0x0C3E, 0x0000,
    0x0000, 0x0000,
    0x33C2, 0x0000,
    0x2961, 0x0000,
    0x2271, 0x0000,
    0x2001, 0x0000,
    0x2271, 0x0000,
    0x2961, 0x0000,
    0x33C2, 0x0000,
    0x0000, 0x0000,
    0x0C3E, 0x0000,
    0x169F, 0x0000,
    0x1D8F, 0x0000,
    0x1FFF, 0x0000,
    0x1D8F, 0x0000,
    0x169F, 0x0000,
    0x0C3E, 0x0000,
    0x0000, 0x0000,
    0x33C2, 0x0000,
    0x2961, 0x0000,
    0x2271, 0x0000,
    0x2001, 0x0000,
    0x2271, 0x0000,
    0x2961, 0x0000,
    0x33C2, 0x0000,
    0x0000, 0x0000,
    0x0C3E, 0x0000,
    0x169F, 0x0000,
    0x1D8F, 0x0000,
    0x1FFF, 0x0000,
    0x1D8F, 0x0000,
    0x169F, 0x0000,
    0x0C3E, 0x0000,
    0x0000, 0x0000,
    0x33C2, 0x0000,
    0x2961, 0x0000,
    0x2271, 0x0000,
    0x2001, 0x0000,
    0x2271, 0x0000,
    0x2961, 0x0000,
    0x33C2, 0x0000,
    0x0000, 0x0000,
    0x0C3E, 0x0000,
    0x169F, 0x0000,
    0x1D8F, 0x0000
};

#define IQ14_COUNT      ((int)(sizeof(IQ_WORDS_14B)/sizeof(IQ_WORDS_14B[0])))
#define IQ_PAIR_COUNT   (IQ14_COUNT/2)

/* =========================================================
 * Lookup by BASEADDR
 * ========================================================= */
extern XSpi_Config XSpi_ConfigTable[];
#ifndef XPAR_XSPI_NUM_INSTANCES
# error "XPAR_XSPI_NUM_INSTANCES not defined"
#endif

static XSpi_Config* XSpi_LookupConfigByBaseAddr(UINTPTR BaseAddr)
{
    for (int i = 0; i < XPAR_XSPI_NUM_INSTANCES; i++) {
        if ((UINTPTR)XSpi_ConfigTable[i].BaseAddress == BaseAddr) {
            return &XSpi_ConfigTable[i];
        }
    }
    return NULL;
}

extern XAxiDma_Config XAxiDma_ConfigTable[];
#ifndef XPAR_XAXIDMA_NUM_INSTANCES
# error "XPAR_XAXIDMA_NUM_INSTANCES not defined"
#endif

static XAxiDma_Config* XAxiDma_LookupConfigByBaseAddr(UINTPTR BaseAddr)
{
    for (int i = 0; i < XPAR_XAXIDMA_NUM_INSTANCES; i++) {
        if ((UINTPTR)XAxiDma_ConfigTable[i].BaseAddr == BaseAddr) {
            return &XAxiDma_ConfigTable[i];
        }
    }
    return NULL;
}

/* =========================================================
 * SPI helpers
 * ========================================================= */
static int SpiInit(void)
{
    XSpi_Config *CfgPtr = XSpi_LookupConfigByBaseAddr(SPI_BASEADDR);
    if (!CfgPtr) {
        xil_printf("ERROR: XSpi config not found for BASEADDR=0x%08lx\r\n",
                   (unsigned long)SPI_BASEADDR);
        return XST_FAILURE;
    }

    int st = XSpi_CfgInitialize(&Spi, CfgPtr, CfgPtr->BaseAddress);
    if (st != XST_SUCCESS) return st;

    st = XSpi_SetOptions(&Spi, XSP_MASTER_OPTION | XSP_MANUAL_SSELECT_OPTION);
    if (st != XST_SUCCESS) return st;

    XSpi_Start(&Spi);
    XSpi_IntrGlobalDisable(&Spi);
    XSpi_SetSlaveSelect(&Spi, SPI_SS_MASK);

    return XST_SUCCESS;
}

static void rf215_write8(uint16_t addr, uint8_t val)
{
    uint16_t cmd = RF215_CMD_WRITE(addr);
    uint8_t tx[3] = { (uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF), val };
    uint8_t rx[3] = {0};
    XSpi_Transfer(&Spi, tx, rx, 3);
}

static uint8_t rf215_read8(uint16_t addr)
{
    uint16_t cmd = RF215_CMD_READ(addr);
    uint8_t tx[3] = { (uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF), 0x00 };
    uint8_t rx[3] = {0};
    XSpi_Transfer(&Spi, tx, rx, 3);
    return rx[2];
}

/* =========================================================
 * DMA init
 * ========================================================= */
static int DmaInit(void)
{
    XAxiDma_Config *CfgPtr = XAxiDma_LookupConfigByBaseAddr(DMA_BASEADDR);
    if (!CfgPtr) {
        xil_printf("ERROR: XAxiDma config not found for BASEADDR=0x%08lx\r\n",
                   (unsigned long)DMA_BASEADDR);
        return XST_FAILURE;
    }

    int st = XAxiDma_CfgInitialize(&AxiDma, CfgPtr);
    if (st != XST_SUCCESS) return st;

    if (XAxiDma_HasSg(&AxiDma)) {
        xil_printf("ERROR: DMA is SG mode (expected Simple mode)\r\n");
        return XST_FAILURE;
    }

    XAxiDma_IntrDisable(&AxiDma, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);
    return XST_SUCCESS;
}

/* =========================================================
 * Synchronization: [Isync=10][I14][Qsync=01][Q14]
 * ========================================================= */
static inline u32 pack_iq_sync(u16 i14, u16 q14)
{
    u32 I = (u32)(i14 & 0x3FFFu);
    u32 Q = (u32)(q14 & 0x3FFFu);
    return (0x2u << 30) | (I << 16) | (0x1u << 14) | Q;
}

/* =========================================================
 * Build DMA buffer:
 *   - 32 words 0x00000000 (preamble)
 *   - IQ data words (relleno con tu secuencia en loop)
 *   - 16 words 0x00000000 (trailer)
 *
 * Nota: el streaming es continuo; el “fin” es el final del buffer,
 *       por eso el trailer queda al final del array.
 * ========================================================= */
static void build_iq_buffer(u32 *tx)
{
    if ((PRE_ZERO_WORDS + POST_ZERO_WORDS) >= IQ_WORDS_TX) {
        /* configuración inválida: no queda espacio para IQ data */
        for (int k = 0; k < (int)IQ_WORDS_TX; k++) tx[k] = 0x00000000u;
        return;
    }

    const int data_words = (int)IQ_WORDS_TX - (int)PRE_ZERO_WORDS - (int)POST_ZERO_WORDS;

    /* 1) preamble: 32 words de cero */
    for (int k = 0; k < (int)PRE_ZERO_WORDS; k++) {
        tx[k] = 0x00000000u;
    }

    /* 2) IQ payload */
    for (int k = 0; k < data_words; k++) {
        int idx = k % IQ_PAIR_COUNT;
        u16 i14 = IQ_WORDS_14B[2*idx + 0];
        u16 q14 = IQ_WORDS_14B[2*idx + 1];
        tx[(int)PRE_ZERO_WORDS + k] = pack_iq_sync(i14, q14);
    }

    /* 3) trailer: 16 words de cero */
    for (int k = 0; k < (int)POST_ZERO_WORDS; k++) {
        tx[(int)PRE_ZERO_WORDS + data_words + k] = 0x00000000u;
    }
}

/* =========================================================
 * RF config: fc=868MHz + IQ mode + fs=2MHz (SR=2)
 * ========================================================= */
static int rf215_config_fc_868_iq_fs2m_and_tx(void)
{
    xil_printf("\n--- AT86RF215 CONFIG: fc=868MHz, IQ mode, fs=2MHz ---\r\n");

    uint8_t pn = rf215_read8(RF_PN_ADDR);
    xil_printf("RF_PN=0x%02X\r\n", pn);
    if (pn != 0x34) {
        xil_printf("ERROR: AT86RF215 not detected\r\n");
        return XST_FAILURE;
    }

    /* TRXOFF */
    rf215_write8(RF09_CMD, CMD_TRXOFF);
    usleep(2000);

    /* fc = 868MHz (25kHz grid) */
    rf215_write8(RF09_CS,    0x01);
    rf215_write8(RF09_CCF0L, 0xA0);
    rf215_write8(RF09_CCF0H, 0x87);
    rf215_write8(RF09_CNL,   0x00);
    rf215_write8(RF09_CNM,   0x00); /* APPLY */

    /* ===== IQ interface setup (Global) =====
       - CHPM=1 => IQ mode
       - EEC=0
       - IQIFC2: lectura de SYNC */
    {
        uint8_t iqifc1 = rf215_read8(RF_IQIFC1_ADDR);
        iqifc1 = (uint8_t)((iqifc1 & ~IQIFC1_CHPM_MASK) | (IQIFC1_CHPM_IQMODE << IQIFC1_CHPM_SHIFT));
        rf215_write8(RF_IQIFC1_ADDR, iqifc1);

        uint8_t iqifc0 = rf215_read8(RF_IQIFC0_ADDR);
        iqifc0 = (uint8_t)(iqifc0 & ~IQIFC0_EEC_MASK);
        rf215_write8(RF_IQIFC0_ADDR, iqifc0);

        rf215_write8(RF_IQIFC2_ADDR, 0x0B);
    }

    /* TXDFE: SR=2 => fs=2MHz (según fs=4MHz/SR) */
    {
        uint8_t rcut = 0x4;
        uint8_t dm   = 0x0;
        uint8_t sr   = 0x2;     /* CLAVE */
        uint8_t txdfe = (uint8_t)((rcut << 5) | (dm << 4) | (sr & 0x0F));
        rf215_write8(RF09_TXDFE, txdfe);
    }

    rf215_write8(RF09_TXCUTC, 0x0B);
    rf215_write8(RF09_PAC, 0x7F);

    /* TXPREP */
    rf215_write8(RF09_CMD, CMD_TXPREP);
    usleep(5000);

    {
        uint8_t v = rf215_read8(RF_IQIFC2_ADDR);
        xil_printf("RF_IQIFC2=0x%02X (SYNC=%d)\r\n", v, (v & IQIFC2_SYNC_MASK) ? 1 : 0);
    }

    /* TX */
    rf215_write8(RF09_CMD, CMD_TX);

    xil_printf("RF09 TX ON. Expect tone @ fc ± 125kHz (IQ @ 2Msps)\r\n");
    return XST_SUCCESS;
}

/* =========================================================
 * MAIN
 * ========================================================= */
int main(void)
{
    init_platform();
    xil_printf("\n=== AT86RF215 IQ TX | fc=868MHz | fs=2MHz | fo=125kHz ===\r\n");
    xil_printf("Buffer: PRE_ZERO=%u, POST_ZERO=%u, TOTAL=%u words\r\n",
               (unsigned)PRE_ZERO_WORDS, (unsigned)POST_ZERO_WORDS, (unsigned)IQ_WORDS_TX);

    if (SpiInit() != XST_SUCCESS) {
        xil_printf("FATAL: SPI init failed\r\n");
        cleanup_platform();
        return XST_FAILURE;
    }

    if (DmaInit() != XST_SUCCESS) {
        xil_printf("FATAL: DMA init failed\r\n");
        cleanup_platform();
        return XST_FAILURE;
    }

    if (rf215_config_fc_868_iq_fs2m_and_tx() != XST_SUCCESS) {
        xil_printf("FATAL: RF215 config failed\r\n");
        cleanup_platform();
        return XST_FAILURE;
    }

    /* Build IQ buffer (with preamble/trailer) */
    build_iq_buffer(tx_buf);
    Xil_DCacheFlushRange((UINTPTR)tx_buf, BYTES_TX);

    xil_printf("Starting continuous DMA streaming (IQ tone)...\r\n");

    while (1) {
        int st = XAxiDma_SimpleTransfer(&AxiDma, (UINTPTR)tx_buf, BYTES_TX, XAXIDMA_DMA_TO_DEVICE);
        if (st != XST_SUCCESS) {
            xil_printf("DMA ERROR: SimpleTransfer failed\r\n");
            break;
        }
        while (XAxiDma_Busy(&AxiDma, XAXIDMA_DMA_TO_DEVICE)) { /* wait */ }
    }

    cleanup_platform();
    return 0;
}
