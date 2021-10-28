/* Copyright (C) 2009 - 2017 National Aeronautics and Space Administration. All Foreign Rights are Reserved to the U.S. Government.

This software is provided "as is" without any warranty of any, kind either express, implied, or statutory, including, but not
limited to, any warranty that the software will conform to, specifications any implied warranties of merchantability, fitness
for a particular purpose, and freedom from infringement, and any warranty that the documentation will conform to the program, or
any warranty that the software will be error free.

In no event shall NASA be liable for any damages, including, but not limited to direct, indirect, special or consequential damages,
arising out of, resulting from, or in any0 way connected with the software or its documentation.  Whether or not based upon warranty,
contract, tort or otherwise, and whether or not loss was sustained from, or arose out of the results of, or use of, the software,
documentation or services provided hereunder

ITC Team
NASA IV&V
ivv-itc@lists.nasa.gov
*/
#ifndef _crypto_c_
#define _crypto_c_

/*
** Includes
*/
#include "crypto.h"

#include "itc_aes128.h"
#include "itc_gcm128.h"

#include "crypto_structs.h"
#include "crypto_print.h"
#include "crypto_config.h"
#include "crypto_events.h"



#include <gcrypt.h>


/*
** Static Library Declaration
*/
#ifdef BUILD_STATIC
    CFS_MODULE_DECLARE_LIB(crypto);
#endif

/*
** Static Prototypes
*/
// Initialization Functions
static int32 Crypto_SA_init(void);
static int32 Crypto_SA_config(void);
// Assisting Functions
static int32  Crypto_Get_tcPayloadLength(void);
static int32  Crypto_Get_tmLength(int len);
static void   Crypto_TM_updatePDU(char* ingest, int len_ingest);
static void   Crypto_TM_updateOCF(void);
//static int32  Crypto_gcm_err(int gcm_err);
static int32  Crypto_increment(uint8* num, int length);
static int32 Crypto_window(uint8 *actual, uint8 *expected, int length, int window);
static int32 Crypto_compare_less_equal(uint8 *actual, uint8 *expected, int length);
static uint8  Crypto_Prep_Reply(char*, uint8);
static int32  Crypto_FECF(int fecf, char* ingest, int len_ingest);
static uint16 Crypto_Calc_FECF(char* ingest, int len_ingest);
static void   Crypto_Calc_CRC_Init_Table(void);
static uint16 Crypto_Calc_CRC16(char* data, int size);
// Key Management Functions
static int32 Crypto_Key_OTAR(void);
static int32 Crypto_Key_update(uint8 state);
static int32 Crypto_Key_inventory(char*);
static int32 Crypto_Key_verify(char*);
// Security Association Functions
static int32 Crypto_SA_stop(void);
static int32 Crypto_SA_start(void);
static int32 Crypto_SA_expire(void);
static int32 Crypto_SA_rekey(void);
static int32 Crypto_SA_status(char*);
static int32 Crypto_SA_create(void);
static int32 Crypto_SA_setARSN(void);
static int32 Crypto_SA_setARSNW(void);
static int32 Crypto_SA_delete(void);
// Security Monitoring & Control Procedure
static int32 Crypto_MC_ping(char* ingest);
static int32 Crypto_MC_status(char* ingest);
static int32 Crypto_MC_dump(char* ingest);
static int32 Crypto_MC_erase(char* ingest);
static int32 Crypto_MC_selftest(char* ingest);
static int32 Crypto_SA_readARSN(char* ingest);
static int32 Crypto_MC_resetalarm(void);
// User Functions
static int32 Crypto_User_IdleTrigger(char* ingest);
static int32 Crypto_User_BadSPI(void);
static int32 Crypto_User_BadIV(void);
static int32 Crypto_User_BadMAC(void);
static int32 Crypto_User_BadFECF(void);
static int32 Crypto_User_ModifyKey(void);
static int32 Crypto_User_ModifyActiveTM(void);
static int32 Crypto_User_ModifyVCID(void);
// Determine Payload Data Unit
static int32 Crypto_PDU(char* ingest);

/*
** Global Variables
*/
// Security
static SecurityAssociation_t sa[NUM_SA];
SecurityAssociation_t *pSA = &sa;
static crypto_key_t ek_ring[NUM_KEYS];
//static crypto_key_t ak_ring[NUM_KEYS];
// Local Frames
static TC_t tc_frame;
static CCSDS_t sdls_frame;
static TM_t tm_frame;
// OCF
static uint8 ocf = 0;
static SDLS_FSR_t report;
static TM_FrameCLCW_t clcw;
// Flags
static SDLS_MC_LOG_RPLY_t log_summary;
static SDLS_MC_DUMP_BLK_RPLY_t log;
static uint8 log_count = 0;
static uint16 tm_offset = 0;
// ESA Testing - 0 = disabled, 1 = enabled
static uint8 badSPI = 0;
static uint8 badIV = 0;
static uint8 badMAC = 0;
static uint8 badFECF = 0;
//  CRC
static uint32 crc32Table[256];
static uint16 crc16Table[256];

/*
** Initialization Functions
*/
static int32 Crypto_SA_init(void)
// General security association initialization
{
    int32 status = OS_SUCCESS;

    for (int x = 0; x < NUM_SA; x++)
    {
        sa[x].ekid = x;
        sa[x].akid = x;
        sa[x].sa_state = SA_NONE;
        sa[x].ecs_len = 0;
        sa[x].ecs[0] = 0;
        sa[x].ecs[1] = 0;
        sa[x].ecs[2] = 0;
        sa[x].ecs[3] = 0;
        sa[x].iv_len = 0;
        sa[x].acs_len = 0;
        sa[x].acs = 0;
        sa[x].arc_len = 0;
        sa[x].arc[0] = 5;
    }

    // Initialize TM Frame
        // TM Header
        tm_frame.tm_header.tfvn    = 0;	    // Shall be 00 for TM-/TC-SDLP
        tm_frame.tm_header.scid    = SCID & 0x3FF; 
        tm_frame.tm_header.vcid    = 0; 
        tm_frame.tm_header.ocff    = 1;
        tm_frame.tm_header.mcfc    = 1;
        tm_frame.tm_header.vcfc    = 1;
        tm_frame.tm_header.tfsh    = 0;
        tm_frame.tm_header.sf      = 0;
        tm_frame.tm_header.pof     = 0;	    // Shall be set to 0
        tm_frame.tm_header.slid    = 3;	    // Shall be set to 11
        tm_frame.tm_header.fhp     = 0;
        // TM Security Header
        tm_frame.tm_sec_header.spi = 0x0000;
        for ( int x = 0; x < IV_SIZE; x++)
        { 	// Initialization Vector
            tm_frame.tm_sec_header.iv[x] = 0x00;
        }
        // TM Payload Data Unit
        for ( int x = 0; x < TM_FRAME_DATA_SIZE; x++)
        {	// Zero TM PDU
            tm_frame.tm_pdu[x] = 0x00;
        }
        // TM Security Trailer
        for ( int x = 0; x < MAC_SIZE; x++)
        { 	// Zero TM Message Authentication Code
            tm_frame.tm_sec_trailer.mac[x] = 0x00;
        }
        for ( int x = 0; x < OCF_SIZE; x++)
        { 	// Zero TM Operational Control Field
            tm_frame.tm_sec_trailer.ocf[x] = 0x00;
        }
        tm_frame.tm_sec_trailer.fecf = 0xFECF;

    // Initialize CLCW
        clcw.cwt 	= 0;			// Control Word Type "0"
        clcw.cvn	= 0;			// CLCW Version Number "00"
        clcw.sf  	= 0;    		// Status Field
        clcw.cie 	= 1;			// COP In Effect
        clcw.vci 	= 0;    		// Virtual Channel Identification
        clcw.spare0 = 0;			// Reserved Spare
        clcw.nrfa	= 0;			// No RF Avaliable Flag
        clcw.nbl	= 0;			// No Bit Lock Flag
        clcw.lo		= 0;			// Lock-Out Flag
        clcw.wait	= 0;			// Wait Flag
        clcw.rt		= 0;			// Retransmit Flag
        clcw.fbc	= 0;			// FARM-B Counter
        clcw.spare1 = 0;			// Reserved Spare
        clcw.rv		= 0;        	// Report Value

    // Initialize Frame Security Report
        report.cwt   = 1;			// Control Word Type "0b1""
        report.vnum  = 4;   		// FSR Version "0b100""
        report.af    = 0;			// Alarm Field
        report.bsnf  = 0;			// Bad SN Flag
        report.bmacf = 0;			// Bad MAC Flag
        report.ispif = 0;			// Invalid SPI Flag
        report.lspiu = 0;	    	// Last SPI Used
        report.snval = 0;			// SN Value (LSB)

    return status;
}

static int32 Crypto_SA_config(void)
// Initialize the mission specific security associations.
// Only need to initialize non-zero values.
{   
    int32 status = OS_SUCCESS;
    
    // Master Keys
        // 0 - 000102030405060708090A0B0C0D0E0F000102030405060708090A0B0C0D0E0F -> ACTIVE
        ek_ring[0].value[0]  = 0x00;
        ek_ring[0].value[1]  = 0x01;
        ek_ring[0].value[2]  = 0x02;
        ek_ring[0].value[3]  = 0x03;
        ek_ring[0].value[4]  = 0x04;
        ek_ring[0].value[5]  = 0x05;
        ek_ring[0].value[6]  = 0x06;
        ek_ring[0].value[7]  = 0x07;
        ek_ring[0].value[8]  = 0x08;
        ek_ring[0].value[9]  = 0x09;
        ek_ring[0].value[10] = 0x0A;
        ek_ring[0].value[11] = 0x0B;
        ek_ring[0].value[12] = 0x0C;
        ek_ring[0].value[13] = 0x0D;
        ek_ring[0].value[14] = 0x0E;
        ek_ring[0].value[15] = 0x0F;
        ek_ring[0].value[16] = 0x00;
        ek_ring[0].value[17] = 0x01;
        ek_ring[0].value[18] = 0x02;
        ek_ring[0].value[19] = 0x03;
        ek_ring[0].value[20] = 0x04;
        ek_ring[0].value[21] = 0x05;
        ek_ring[0].value[22] = 0x06;
        ek_ring[0].value[23] = 0x07;
        ek_ring[0].value[24] = 0x08;
        ek_ring[0].value[25] = 0x09;
        ek_ring[0].value[26] = 0x0A;
        ek_ring[0].value[27] = 0x0B;
        ek_ring[0].value[28] = 0x0C;
        ek_ring[0].value[29] = 0x0D;
        ek_ring[0].value[30] = 0x0E;
        ek_ring[0].value[31] = 0x0F;
        ek_ring[0].key_state = KEY_ACTIVE;
        // 1 - 101112131415161718191A1B1C1D1E1F101112131415161718191A1B1C1D1E1F -> ACTIVE
        ek_ring[1].value[0]  = 0x10;
        ek_ring[1].value[1]  = 0x11;
        ek_ring[1].value[2]  = 0x12;
        ek_ring[1].value[3]  = 0x13;
        ek_ring[1].value[4]  = 0x14;
        ek_ring[1].value[5]  = 0x15;
        ek_ring[1].value[6]  = 0x16;
        ek_ring[1].value[7]  = 0x17;
        ek_ring[1].value[8]  = 0x18;
        ek_ring[1].value[9]  = 0x19;
        ek_ring[1].value[10] = 0x1A;
        ek_ring[1].value[11] = 0x1B;
        ek_ring[1].value[12] = 0x1C;
        ek_ring[1].value[13] = 0x1D;
        ek_ring[1].value[14] = 0x1E;
        ek_ring[1].value[15] = 0x1F;
        ek_ring[1].value[16] = 0x10;
        ek_ring[1].value[17] = 0x11;
        ek_ring[1].value[18] = 0x12;
        ek_ring[1].value[19] = 0x13;
        ek_ring[1].value[20] = 0x14;
        ek_ring[1].value[21] = 0x15;
        ek_ring[1].value[22] = 0x16;
        ek_ring[1].value[23] = 0x17;
        ek_ring[1].value[24] = 0x18;
        ek_ring[1].value[25] = 0x19;
        ek_ring[1].value[26] = 0x1A;
        ek_ring[1].value[27] = 0x1B;
        ek_ring[1].value[28] = 0x1C;
        ek_ring[1].value[29] = 0x1D;
        ek_ring[1].value[30] = 0x1E;
        ek_ring[1].value[31] = 0x1F;
        ek_ring[1].key_state = KEY_ACTIVE;
        // 2 - 202122232425262728292A2B2C2D2E2F202122232425262728292A2B2C2D2E2F -> ACTIVE
        ek_ring[2].value[0]  = 0x20;
        ek_ring[2].value[1]  = 0x21;
        ek_ring[2].value[2]  = 0x22;
        ek_ring[2].value[3]  = 0x23;
        ek_ring[2].value[4]  = 0x24;
        ek_ring[2].value[5]  = 0x25;
        ek_ring[2].value[6]  = 0x26;
        ek_ring[2].value[7]  = 0x27;
        ek_ring[2].value[8]  = 0x28;
        ek_ring[2].value[9]  = 0x29;
        ek_ring[2].value[10] = 0x2A;
        ek_ring[2].value[11] = 0x2B;
        ek_ring[2].value[12] = 0x2C;
        ek_ring[2].value[13] = 0x2D;
        ek_ring[2].value[14] = 0x2E;
        ek_ring[2].value[15] = 0x2F;
        ek_ring[2].value[16] = 0x20;
        ek_ring[2].value[17] = 0x21;
        ek_ring[2].value[18] = 0x22;
        ek_ring[2].value[19] = 0x23;
        ek_ring[2].value[20] = 0x24;
        ek_ring[2].value[21] = 0x25;
        ek_ring[2].value[22] = 0x26;
        ek_ring[2].value[23] = 0x27;
        ek_ring[2].value[24] = 0x28;
        ek_ring[2].value[25] = 0x29;
        ek_ring[2].value[26] = 0x2A;
        ek_ring[2].value[27] = 0x2B;
        ek_ring[2].value[28] = 0x2C;
        ek_ring[2].value[29] = 0x2D;
        ek_ring[2].value[30] = 0x2E;
        ek_ring[2].value[31] = 0x2F;
        ek_ring[2].key_state = KEY_ACTIVE;

    // Session Keys
        // 128 - 0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF -> ACTIVE
        ek_ring[128].value[0]  = 0x01;
        ek_ring[128].value[1]  = 0x23;
        ek_ring[128].value[2]  = 0x45;
        ek_ring[128].value[3]  = 0x67;
        ek_ring[128].value[4]  = 0x89;
        ek_ring[128].value[5]  = 0xAB;
        ek_ring[128].value[6]  = 0xCD;
        ek_ring[128].value[7]  = 0xEF;
        ek_ring[128].value[8]  = 0x01;
        ek_ring[128].value[9]  = 0x23;
        ek_ring[128].value[10] = 0x45;
        ek_ring[128].value[11] = 0x67;
        ek_ring[128].value[12] = 0x89;
        ek_ring[128].value[13] = 0xAB;
        ek_ring[128].value[14] = 0xCD;
        ek_ring[128].value[15] = 0xEF;
        ek_ring[128].value[16] = 0x01;
        ek_ring[128].value[17] = 0x23;
        ek_ring[128].value[18] = 0x45;
        ek_ring[128].value[19] = 0x67;
        ek_ring[128].value[20] = 0x89;
        ek_ring[128].value[21] = 0xAB;
        ek_ring[128].value[22] = 0xCD;
        ek_ring[128].value[23] = 0xEF;
        ek_ring[128].value[24] = 0x01;
        ek_ring[128].value[25] = 0x23;
        ek_ring[128].value[26] = 0x45;
        ek_ring[128].value[27] = 0x67;
        ek_ring[128].value[28] = 0x89;
        ek_ring[128].value[29] = 0xAB;
        ek_ring[128].value[30] = 0xCD;
        ek_ring[128].value[31] = 0xEF;
        ek_ring[128].key_state = KEY_ACTIVE;
        // 129 - ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789 -> ACTIVE
        ek_ring[129].value[0]  = 0xAB;
        ek_ring[129].value[1]  = 0xCD;
        ek_ring[129].value[2]  = 0xEF;
        ek_ring[129].value[3]  = 0x01;
        ek_ring[129].value[4]  = 0x23;
        ek_ring[129].value[5]  = 0x45;
        ek_ring[129].value[6]  = 0x67;
        ek_ring[129].value[7]  = 0x89;
        ek_ring[129].value[8]  = 0xAB;
        ek_ring[129].value[9]  = 0xCD;
        ek_ring[129].value[10] = 0xEF;
        ek_ring[129].value[11] = 0x01;
        ek_ring[129].value[12] = 0x23;
        ek_ring[129].value[13] = 0x45;
        ek_ring[129].value[14] = 0x67;
        ek_ring[129].value[15] = 0x89;
        ek_ring[129].value[16] = 0xAB;
        ek_ring[129].value[17] = 0xCD;
        ek_ring[129].value[18] = 0xEF;
        ek_ring[129].value[19] = 0x01;
        ek_ring[129].value[20] = 0x23;
        ek_ring[129].value[21] = 0x45;
        ek_ring[129].value[22] = 0x67;
        ek_ring[129].value[23] = 0x89;
        ek_ring[129].value[24] = 0xAB;
        ek_ring[129].value[25] = 0xCD;
        ek_ring[129].value[26] = 0xEF;
        ek_ring[129].value[27] = 0x01;
        ek_ring[129].value[28] = 0x23;
        ek_ring[129].value[29] = 0x45;
        ek_ring[129].value[30] = 0x67;
        ek_ring[129].value[31] = 0x89;
        ek_ring[129].key_state = KEY_ACTIVE;
        // 130 - FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210 -> ACTIVE
        ek_ring[130].value[0]  = 0xFE;
        ek_ring[130].value[1]  = 0xDC;
        ek_ring[130].value[2]  = 0xBA;
        ek_ring[130].value[3]  = 0x98;
        ek_ring[130].value[4]  = 0x76;
        ek_ring[130].value[5]  = 0x54;
        ek_ring[130].value[6]  = 0x32;
        ek_ring[130].value[7]  = 0x10;
        ek_ring[130].value[8]  = 0xFE;
        ek_ring[130].value[9]  = 0xDC;
        ek_ring[130].value[10] = 0xBA;
        ek_ring[130].value[11] = 0x98;
        ek_ring[130].value[12] = 0x76;
        ek_ring[130].value[13] = 0x54;
        ek_ring[130].value[14] = 0x32;
        ek_ring[130].value[15] = 0x10;
        ek_ring[130].value[16] = 0xFE;
        ek_ring[130].value[17] = 0xDC;
        ek_ring[130].value[18] = 0xBA;
        ek_ring[130].value[19] = 0x98;
        ek_ring[130].value[20] = 0x76;
        ek_ring[130].value[21] = 0x54;
        ek_ring[130].value[22] = 0x32;
        ek_ring[130].value[23] = 0x10;
        ek_ring[130].value[24] = 0xFE;
        ek_ring[130].value[25] = 0xDC;
        ek_ring[130].value[26] = 0xBA;
        ek_ring[130].value[27] = 0x98;
        ek_ring[130].value[28] = 0x76;
        ek_ring[130].value[29] = 0x54;
        ek_ring[130].value[30] = 0x32;
        ek_ring[130].value[31] = 0x10;
        ek_ring[130].key_state = KEY_ACTIVE;
        // 131 - 9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA -> ACTIVE
        ek_ring[131].value[0]  = 0x98;
        ek_ring[131].value[1]  = 0x76;
        ek_ring[131].value[2]  = 0x54;
        ek_ring[131].value[3]  = 0x32;
        ek_ring[131].value[4]  = 0x10;
        ek_ring[131].value[5]  = 0xFE;
        ek_ring[131].value[6]  = 0xDC;
        ek_ring[131].value[7]  = 0xBA;
        ek_ring[131].value[8]  = 0x98;
        ek_ring[131].value[9]  = 0x76;
        ek_ring[131].value[10] = 0x54;
        ek_ring[131].value[11] = 0x32;
        ek_ring[131].value[12] = 0x10;
        ek_ring[131].value[13] = 0xFE;
        ek_ring[131].value[14] = 0xDC;
        ek_ring[131].value[15] = 0xBA;
        ek_ring[131].value[16] = 0x98;
        ek_ring[131].value[17] = 0x76;
        ek_ring[131].value[18] = 0x54;
        ek_ring[131].value[19] = 0x32;
        ek_ring[131].value[20] = 0x10;
        ek_ring[131].value[21] = 0xFE;
        ek_ring[131].value[22] = 0xDC;
        ek_ring[131].value[23] = 0xBA;
        ek_ring[131].value[24] = 0x98;
        ek_ring[131].value[25] = 0x76;
        ek_ring[131].value[26] = 0x54;
        ek_ring[131].value[27] = 0x32;
        ek_ring[131].value[28] = 0x10;
        ek_ring[131].value[29] = 0xFE;
        ek_ring[131].value[30] = 0xDC;
        ek_ring[131].value[31] = 0xBA;
        ek_ring[131].key_state = KEY_ACTIVE;
        // 132 - 0123456789ABCDEFABCDEF01234567890123456789ABCDEFABCDEF0123456789 -> PRE_ACTIVATION
        ek_ring[132].value[0]  = 0x01;
        ek_ring[132].value[1]  = 0x23;
        ek_ring[132].value[2]  = 0x45;
        ek_ring[132].value[3]  = 0x67;
        ek_ring[132].value[4]  = 0x89;
        ek_ring[132].value[5]  = 0xAB;
        ek_ring[132].value[6]  = 0xCD;
        ek_ring[132].value[7]  = 0xEF;
        ek_ring[132].value[8]  = 0xAB;
        ek_ring[132].value[9]  = 0xCD;
        ek_ring[132].value[10] = 0xEF;
        ek_ring[132].value[11] = 0x01;
        ek_ring[132].value[12] = 0x23;
        ek_ring[132].value[13] = 0x45;
        ek_ring[132].value[14] = 0x67;
        ek_ring[132].value[15] = 0x89;
        ek_ring[132].value[16] = 0x01;
        ek_ring[132].value[17] = 0x23;
        ek_ring[132].value[18] = 0x45;
        ek_ring[132].value[19] = 0x67;
        ek_ring[132].value[20] = 0x89;
        ek_ring[132].value[21] = 0xAB;
        ek_ring[132].value[22] = 0xCD;
        ek_ring[132].value[23] = 0xEF;
        ek_ring[132].value[24] = 0xAB;
        ek_ring[132].value[25] = 0xCD;
        ek_ring[132].value[26] = 0xEF;
        ek_ring[132].value[27] = 0x01;
        ek_ring[132].value[28] = 0x23;
        ek_ring[132].value[29] = 0x45;
        ek_ring[132].value[30] = 0x67;
        ek_ring[132].value[31] = 0x89;
        ek_ring[132].key_state = KEY_PREACTIVE;
        // 133 - ABCDEF01234567890123456789ABCDEFABCDEF01234567890123456789ABCDEF -> ACTIVE
        ek_ring[133].value[0]  = 0xAB;
        ek_ring[133].value[1]  = 0xCD;
        ek_ring[133].value[2]  = 0xEF;
        ek_ring[133].value[3]  = 0x01;
        ek_ring[133].value[4]  = 0x23;
        ek_ring[133].value[5]  = 0x45;
        ek_ring[133].value[6]  = 0x67;
        ek_ring[133].value[7]  = 0x89;
        ek_ring[133].value[8]  = 0x01;
        ek_ring[133].value[9]  = 0x23;
        ek_ring[133].value[10] = 0x45;
        ek_ring[133].value[11] = 0x67;
        ek_ring[133].value[12] = 0x89;
        ek_ring[133].value[13] = 0xAB;
        ek_ring[133].value[14] = 0xCD;
        ek_ring[133].value[15] = 0xEF;
        ek_ring[133].value[16] = 0xAB;
        ek_ring[133].value[17] = 0xCD;
        ek_ring[133].value[18] = 0xEF;
        ek_ring[133].value[19] = 0x01;
        ek_ring[133].value[20] = 0x23;
        ek_ring[133].value[21] = 0x45;
        ek_ring[133].value[22] = 0x67;
        ek_ring[133].value[23] = 0x89;
        ek_ring[133].value[24] = 0x01;
        ek_ring[133].value[25] = 0x23;
        ek_ring[133].value[26] = 0x45;
        ek_ring[133].value[27] = 0x67;
        ek_ring[133].value[28] = 0x89;
        ek_ring[133].value[29] = 0xAB;
        ek_ring[133].value[30] = 0xCD;
        ek_ring[133].value[31] = 0xEF;
        ek_ring[133].key_state = KEY_ACTIVE;
        // 134 - ABCDEF0123456789FEDCBA9876543210ABCDEF0123456789FEDCBA9876543210 -> DEACTIVE
        ek_ring[134].value[0]  = 0xAB;
        ek_ring[134].value[1]  = 0xCD;
        ek_ring[134].value[2]  = 0xEF;
        ek_ring[134].value[3]  = 0x01;
        ek_ring[134].value[4]  = 0x23;
        ek_ring[134].value[5]  = 0x45;
        ek_ring[134].value[6]  = 0x67;
        ek_ring[134].value[7]  = 0x89;
        ek_ring[134].value[8]  = 0xFE;
        ek_ring[134].value[9]  = 0xDC;
        ek_ring[134].value[10] = 0xBA;
        ek_ring[134].value[11] = 0x98;
        ek_ring[134].value[12] = 0x76;
        ek_ring[134].value[13] = 0x54;
        ek_ring[134].value[14] = 0x32;
        ek_ring[134].value[15] = 0x10;
        ek_ring[134].value[16] = 0xAB;
        ek_ring[134].value[17] = 0xCD;
        ek_ring[134].value[18] = 0xEF;
        ek_ring[134].value[19] = 0x01;
        ek_ring[134].value[20] = 0x23;
        ek_ring[134].value[21] = 0x45;
        ek_ring[134].value[22] = 0x67;
        ek_ring[134].value[23] = 0x89;
        ek_ring[134].value[24] = 0xFE;
        ek_ring[134].value[25] = 0xDC;
        ek_ring[134].value[26] = 0xBA;
        ek_ring[134].value[27] = 0x98;
        ek_ring[134].value[28] = 0x76;
        ek_ring[134].value[29] = 0x54;
        ek_ring[134].value[30] = 0x32;
        ek_ring[134].value[31] = 0x10;
        ek_ring[134].key_state = KEY_DEACTIVATED;

    // Security Associations
        // SA 1 - CLEAR MODE
        sa[1].sa_state = SA_OPERATIONAL;
        sa[1].est = 0;
        sa[1].ast = 0;
        sa[1].shplf_len = 2;
        sa[1].arc_len = 0;
        sa[1].arc_len = 1;
        sa[1].arcw_len = 1;
        sa[1].arcw[0] = 5;
        sa[1].gvcid_tc_blk[0].tfvn  = 0;
        sa[1].gvcid_tc_blk[0].scid  = SCID & 0x3FF;
        sa[1].gvcid_tc_blk[0].vcid  = 0;
        sa[1].gvcid_tc_blk[0].mapid = TYPE_TC;
        sa[1].gvcid_tc_blk[1].tfvn  = 0;
        sa[1].gvcid_tc_blk[1].scid  = SCID & 0x3FF;
        sa[1].gvcid_tc_blk[1].vcid  = 1;
        sa[1].gvcid_tc_blk[1].mapid = TYPE_TC;
        // SA 2 - KEYED;  ARCW:5; AES-GCM; IV:00...00; IV-len:12; MAC-len:16; Key-ID: 128
        sa[2].ekid = 128;
        sa[2].sa_state = SA_KEYED;
        sa[2].est = 1; 
        sa[2].ast = 1;
        sa[2].shivf_len = 12;
        sa[2].iv_len = IV_SIZE;
        sa[2].iv[IV_SIZE-1] = 0;
        sa[2].abm_len = 0x14; // 20
        for (int i = 0; i < sa[2].abm_len; i++)
        {	// Zero AAD bit mask
            sa[2].abm[i] = 0x00;
        }
        sa[2].arcw_len = 1;   
        sa[2].arcw[0] = 5;
        sa[2].arc_len = (sa[2].arcw[0] * 2) + 1;
        // SA 3 - KEYED;   ARCW:5; AES-GCM; IV:00...00; IV-len:12; MAC-len:16; Key-ID: 129
        sa[3].ekid = 129;
        sa[3].sa_state = SA_KEYED;
        sa[3].est = 1; 
        sa[3].ast = 1;
        sa[3].shivf_len = 12;
        sa[3].iv_len = IV_SIZE;
        sa[3].iv[IV_SIZE-1] = 0;
        sa[3].abm_len = 0x14; // 20
        for (int i = 0; i < sa[3].abm_len; i++)
        {	// Zero AAD bit mask
            sa[3].abm[i] = 0x00;
        }
        sa[3].arcw_len = 1;   
        sa[3].arcw[0] = 5;
        sa[3].arc_len = (sa[3].arcw[0] * 2) + 1;
        // SA 4 - KEYED;  ARCW:5; AES-GCM; IV:00...00; IV-len:12; MAC-len:16; Key-ID: 130
        sa[4].ekid = 130;
        sa[4].sa_state = SA_KEYED;
        // sa[4].sa_state = SA_OPERATIONAL;
        sa[4].est = 1; 
        sa[4].ast = 1;
        sa[4].shivf_len = 12;
        sa[4].iv_len = IV_SIZE;
        sa[4].iv[IV_SIZE-1] = 0;
        sa[4].abm_len = 0x14; // 20
        for (int i = 0; i < sa[4].abm_len; i++)
        {	// Zero AAD bit mask
            sa[4].abm[i] = 0x00;
        }
        sa[4].arcw_len = 1;   
        sa[4].arcw[0] = 5;
        sa[4].arc_len = (sa[4].arcw[0] * 2) + 1;
        sa[4].gvcid_tc_blk[0].tfvn  = 0;
        sa[4].gvcid_tc_blk[0].scid  = SCID & 0x3FF;
        sa[4].gvcid_tc_blk[0].vcid  = 0;
        sa[4].gvcid_tc_blk[0].mapid = TYPE_TC;
        sa[4].gvcid_tc_blk[1].tfvn  = 0;
        sa[4].gvcid_tc_blk[1].scid  = SCID & 0x3FF;
        sa[4].gvcid_tc_blk[1].vcid  = 1;
        sa[4].gvcid_tc_blk[1].mapid = TYPE_TC;
        // SA 5 - KEYED;   ARCW:5; AES-GCM; IV:00...00; IV-len:12; MAC-len:16; Key-ID: 131
        sa[5].ekid = 131;
        sa[5].sa_state = SA_KEYED;
        sa[5].est = 1; 
        sa[5].ast = 1;
        sa[5].shivf_len = 12;
        sa[5].iv_len = IV_SIZE;
        sa[5].iv[IV_SIZE-1] = 0;
        sa[5].abm_len = 0x14; // 20
        for (int i = 0; i < sa[5].abm_len; i++)
        {	// Zero AAD bit mask
            sa[5].abm[i] = 0x00;
        }
        sa[5].arcw_len = 1;   
        sa[5].arcw[0] = 5;
        sa[5].arc_len = (sa[5].arcw[0] * 2) + 1;
        // SA 6 - UNKEYED; ARCW:5; AES-GCM; IV:00...00; IV-len:12; MAC-len:16; Key-ID: -
        sa[6].sa_state = SA_UNKEYED;
        sa[6].est = 1; 
        sa[6].ast = 1;
        sa[6].shivf_len = 12;
        sa[6].iv_len = IV_SIZE;
        sa[6].iv[IV_SIZE-1] = 0;
        sa[6].abm_len = 0x14; // 20
        for (int i = 0; i < sa[6].abm_len; i++)
        {	// Zero AAD bit mask
            sa[6].abm[i] = 0x00;
        }
        sa[6].arcw_len = 1;   
        sa[6].arcw[0] = 5;
        sa[6].arc_len = (sa[6].arcw[0] * 2) + 1;
        //itc_gcm128_init(&(sa[6].gcm_ctx), (unsigned char *)&(ek_ring[sa[6].ekid]));

    // Initial TM configuration
        tm_frame.tm_sec_header.spi = 1;

    // Initialize Log
        log_summary.num_se = 2;
        log_summary.rs = LOG_SIZE;
        // Add a two messages to the log
        log_summary.rs--;
        log.blk[log_count].emt = STARTUP;
        log.blk[log_count].emv[0] = 0x4E;
        log.blk[log_count].emv[1] = 0x41;
        log.blk[log_count].emv[2] = 0x53;
        log.blk[log_count].emv[3] = 0x41;
        log.blk[log_count++].em_len = 4;
        log_summary.rs--;
        log.blk[log_count].emt = STARTUP;
        log.blk[log_count].emv[0] = 0x4E;
        log.blk[log_count].emv[1] = 0x41;
        log.blk[log_count].emv[2] = 0x53;
        log.blk[log_count].emv[3] = 0x41;
        log.blk[log_count++].em_len = 4;

    return status;
}

int32 Crypto_Init(void)
{   
    int32 status = OS_SUCCESS;

    // Initialize libgcrypt
    if (!gcry_check_version(GCRYPT_VERSION))
    {
        fprintf(stderr, "Gcrypt Version: %s",GCRYPT_VERSION);
        OS_printf(KRED "ERROR: gcrypt version mismatch! \n" RESET);
    }
    if (gcry_control(GCRYCTL_SELFTEST) != GPG_ERR_NO_ERROR)
    {
        OS_printf(KRED "ERROR: gcrypt self test failed\n" RESET);
    }

    gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);

    // Init Security Associations
    status = Crypto_SA_init();
    status = Crypto_SA_config();

    // TODO - Add error checking

    // Init table for CRC calculations
    Crypto_Calc_CRC_Init_Table();

    // cFS Standard Initialized Message
    OS_printf (KBLU "Crypto Lib Intialized.  Version %d.%d.%d.%d\n" RESET,
                CRYPTO_LIB_MAJOR_VERSION,
                CRYPTO_LIB_MINOR_VERSION, 
                CRYPTO_LIB_REVISION, 
                CRYPTO_LIB_MISSION_REV);
                                
    return status; 
}

static void Crypto_Calc_CRC_Init_Table(void)
{   
    uint16 val;
    uint32 poly = 0xEDB88320;
    uint32 crc;

    // http://create.stephan-brumme.com/crc32/
    for (unsigned int i = 0; i <= 0xFF; i++)
    {
        crc = i;
        for (unsigned int j = 0; j < 8; j++)
        {
            crc = (crc >> 1) ^ (-(int)(crc & 1) & poly);
        }
        crc32Table[i] = crc;
        //OS_printf("crc32Table[%d] = 0x%08x \n", i, crc32Table[i]);
    }
    
    // Code provided by ESA
    for (int i = 0; i < 256; i++)
    {
        val = 0;
        if ( (i &   1) != 0 )  val ^= 0x1021;
        if ( (i &   2) != 0 )  val ^= 0x2042;
        if ( (i &   4) != 0 )  val ^= 0x4084;
        if ( (i &   8) != 0 )  val ^= 0x8108;
        if ( (i &  16) != 0 )  val ^= 0x1231;
        if ( (i &  32) != 0 )  val ^= 0x2462;
        if ( (i &  64) != 0 )  val ^= 0x48C4;
        if ( (i & 128) != 0 )  val ^= 0x9188;
        crc16Table[i] = val;
        //OS_printf("crc16Table[%d] = 0x%04x \n", i, crc16Table[i]);
    }
}

/*
** Assisting Functions
*/
static int32 Crypto_Get_tcPayloadLength(void)
// Returns the payload length of current tc_frame in BYTES!
{
    return (tc_frame.tc_header.fl - (5 + 2 + IV_SIZE ) - (MAC_SIZE + FECF_SIZE) );
}

static int32 Crypto_Get_tmLength(int len)
// Returns the total length of the current tm_frame in BYTES!
{
    #ifdef FILL
        len = TM_FILL_SIZE;
    #else
        len = TM_FRAME_PRIMARYHEADER_SIZE + TM_FRAME_SECHEADER_SIZE + len + TM_FRAME_SECTRAILER_SIZE + TM_FRAME_CLCW_SIZE;
    #endif

    return len;
}

static void Crypto_TM_updatePDU(char* ingest, int len_ingest)
// Update the Telemetry Payload Data Unit
{	// Copy ingest to PDU
    int x = 0;
    int fill_size = 0;
    
    if ((sa[tm_frame.tm_sec_header.spi].est == 1) && (sa[tm_frame.tm_sec_header.spi].ast == 1))
    {
        fill_size = 1129 - MAC_SIZE - IV_SIZE + 2; // +2 for padding bytes
    }
    else
    {
        fill_size = 1129;
    }

    #ifdef TM_ZERO_FILL
        for (int x = 0; x < TM_FILL_SIZE; x++)
        {
            if (x < len_ingest)
            {	// Fill
                tm_frame.tm_pdu[x] = (uint8)ingest[x];
            }
            else
            {	// Zero
                tm_frame.tm_pdu[x] = 0x00;
            }
        }
    #else
        // Pre-append remaining packet if exist
        if (tm_offset == 63)
        {
            tm_frame.tm_pdu[x++] = 0xff;
            tm_offset--;
        }
        if (tm_offset == 62)
        {
            tm_frame.tm_pdu[x++] = 0x00;
            tm_offset--;
        }
        if (tm_offset == 61)
        {
            tm_frame.tm_pdu[x++] = 0x00;
            tm_offset--;
        }
        if (tm_offset == 60)
        {
            tm_frame.tm_pdu[x++] = 0x00;
            tm_offset--;
        }
        if (tm_offset == 59)
        {
            tm_frame.tm_pdu[x++] = 0x39;
            tm_offset--;
        }
        while (x < tm_offset)
        {
            tm_frame.tm_pdu[x] = 0x00;
            x++;
        }
        // Copy actual packet
        while (x < len_ingest + tm_offset)
        {
            //OS_printf("ingest[x - tm_offset] = 0x%02x \n", (uint8)ingest[x - tm_offset]);
            tm_frame.tm_pdu[x] = (uint8)ingest[x - tm_offset];
            x++;
        }
        #ifdef TM_IDLE_FILL
            // Check for idle frame trigger
            if (((uint8)ingest[0] == 0x08) && ((uint8)ingest[1] == 0x90))
            { 
                // Don't fill idle frames   
            }
            else
            {
                while (x < (fill_size - 64) )
                {
                    tm_frame.tm_pdu[x++] = 0x07;
                    tm_frame.tm_pdu[x++] = 0xff;
                    tm_frame.tm_pdu[x++] = 0x00;
                    tm_frame.tm_pdu[x++] = 0x00;
                    tm_frame.tm_pdu[x++] = 0x00;
                    tm_frame.tm_pdu[x++] = 0x39;
                    for (int y = 0; y < 58; y++)
                    {
                        tm_frame.tm_pdu[x++] = 0x00;
                    }
                }
                // Add partial packet, if possible, and set offset
                if (x < fill_size)
                {
                    tm_frame.tm_pdu[x++] = 0x07;
                    tm_offset = 63;
                }
                if (x < fill_size)
                {
                    tm_frame.tm_pdu[x++] = 0xff;
                    tm_offset--;
                }
                if (x < fill_size)
                {
                    tm_frame.tm_pdu[x++] = 0x00;
                    tm_offset--;
                }
                if (x < fill_size)
                {
                    tm_frame.tm_pdu[x++] = 0x00;
                    tm_offset--;
                }
                if (x < fill_size)
                {
                    tm_frame.tm_pdu[x++] = 0x00;
                    tm_offset--;
                }
                if (x < fill_size)
                {
                    tm_frame.tm_pdu[x++] = 0x39;
                    tm_offset--;
                }
                for (int y = 0; x < fill_size; y++)
                {
                    tm_frame.tm_pdu[x++] = 00;
                    tm_offset--;
                }
            }
            while (x < TM_FILL_SIZE)
            {
                tm_frame.tm_pdu[x++] = 0x00;
            }
        #endif 
    #endif

    return;
}

static void Crypto_TM_updateOCF(void)
{
    if (ocf == 0)
    {	// CLCW
        clcw.vci = tm_frame.tm_header.vcid;

        tm_frame.tm_sec_trailer.ocf[0] = (clcw.cwt << 7) | (clcw.cvn << 5) | (clcw.sf << 2) | (clcw.cie);
        tm_frame.tm_sec_trailer.ocf[1] = (clcw.vci << 2) | (clcw.spare0);
        tm_frame.tm_sec_trailer.ocf[2] = (clcw.nrfa << 7) | (clcw.nbl << 6) | (clcw.lo << 5) | (clcw.wait << 4) | (clcw.rt << 3) | (clcw.fbc << 1) | (clcw.spare1);
        tm_frame.tm_sec_trailer.ocf[3] = (clcw.rv);
        // Alternate OCF
        ocf = 1;
        #ifdef OCF_DEBUG
            Crypto_clcwPrint(&clcw);
        #endif
    } 
    else
    {	// FSR
        tm_frame.tm_sec_trailer.ocf[0] = (report.cwt << 7) | (report.vnum << 4) | (report.af << 3) | (report.bsnf << 2) | (report.bmacf << 1) | (report.ispif);
        tm_frame.tm_sec_trailer.ocf[1] = (report.lspiu & 0xFF00) >> 8;
        tm_frame.tm_sec_trailer.ocf[2] = (report.lspiu & 0x00FF);
        tm_frame.tm_sec_trailer.ocf[3] = (report.snval);  
        // Alternate OCF
        ocf = 0;
        #ifdef OCF_DEBUG
            Crypto_fsrPrint(&report);
        #endif
    }
}

static int32 Crypto_increment(uint8 *num, int length)
{
    int i;
    /* go from right (least significant) to left (most signifcant) */
    for(i = length - 1; i >= 0; --i)
    {
        ++(num[i]); /* increment current byte */

        if(num[i] != 0) /* if byte did not overflow, we're done! */
           break;
    }

    if(i < 0) /* this means num[0] was incremented and overflowed */
        return OS_ERROR;
    else
        return OS_SUCCESS;
}

static int32 Crypto_window(uint8 *actual, uint8 *expected, int length, int window)
{
    int status = OS_ERROR;
    int result = 0;
    uint8 temp[length];

    CFE_PSP_MemCpy(temp, expected, length);

    for (int i = 0; i < window; i++)
    {   
        result = 0;
        /* go from right (least significant) to left (most signifcant) */
        for (int j = length - 1; j >= 0; --j)
        {
            if (actual[j] == temp[j])
            {
                result++;
            }
        }        
        if (result == length)
        {
            status = OS_SUCCESS;
            break;
        }
        Crypto_increment(&temp[0], length);
    }
    return status;
}

static int32 Crypto_compare_less_equal(uint8 *actual, uint8 *expected, int length)
{
    int status = OS_ERROR;

    for(int i = 0; i < length - 1; i++)
    {
        if (actual[i] > expected[i])
        {
            status = OS_SUCCESS;
            break;
        }
        else if (actual[i] < expected[i])
        {
            status = OS_ERROR;
            break;
        }
    }
    return status;
}

static uint8 Crypto_Prep_Reply(char* ingest, uint8 appID)
// Assumes that both the pkt_length and pdu_len are set properly
{
    uint8 count = 0;
    
    // Prepare CCSDS for reply
    sdls_frame.hdr.pvn   = 0;
    sdls_frame.hdr.type  = 0;
    sdls_frame.hdr.shdr  = 1;
    sdls_frame.hdr.appID = appID;

    sdls_frame.pdu.type	 = 1;
    
    // Fill ingest with reply header
    ingest[count++] = (sdls_frame.hdr.pvn << 5) | (sdls_frame.hdr.type << 4) | (sdls_frame.hdr.shdr << 3) | ((sdls_frame.hdr.appID & 0x700 >> 8));	
    ingest[count++] = (sdls_frame.hdr.appID & 0x00FF);
    ingest[count++] = (sdls_frame.hdr.seq << 6) | ((sdls_frame.hdr.pktid & 0x3F00) >> 8);
    ingest[count++] = (sdls_frame.hdr.pktid & 0x00FF);
    ingest[count++] = (sdls_frame.hdr.pkt_length & 0xFF00) >> 8;
    ingest[count++] = (sdls_frame.hdr.pkt_length & 0x00FF);

    // Fill ingest with PUS
    //ingest[count++] = (sdls_frame.pus.shf << 7) | (sdls_frame.pus.pusv << 4) | (sdls_frame.pus.ack);
    //ingest[count++] = (sdls_frame.pus.st);
    //ingest[count++] = (sdls_frame.pus.sst);
    //ingest[count++] = (sdls_frame.pus.sid << 4) | (sdls_frame.pus.spare);
    
    // Fill ingest with Tag and Length
    ingest[count++] = (sdls_frame.pdu.type << 7) | (sdls_frame.pdu.uf << 6) | (sdls_frame.pdu.sg << 4) | (sdls_frame.pdu.pid);
    ingest[count++] = (sdls_frame.pdu.pdu_len & 0xFF00) >> 8;
    ingest[count++] = (sdls_frame.pdu.pdu_len & 0x00FF);

    return count;
}

static int32 Crypto_FECF(int fecf, char* ingest, int len_ingest)
// Calculate the Frame Error Control Field (FECF), also known as a cyclic redundancy check (CRC)
{
    int32 result = OS_SUCCESS;
    uint16 calc_fecf = Crypto_Calc_FECF(ingest, len_ingest);

    if ( (fecf & 0xFFFF) != calc_fecf )
        {
            if (((uint8)ingest[18] == 0x0B) && ((uint8)ingest[19] == 0x00) && (((uint8)ingest[20] & 0xF0) == 0x40))
            {   
                // User packet check only used for ESA Testing!
            }
            else
            {   // TODO: Error Correction
                OS_printf(KRED "Error: FECF incorrect!\n" RESET);
                if (log_summary.rs > 0)
                {
                    Crypto_increment((uint8*)&log_summary.num_se, 4);
                    log_summary.rs--;
                    log.blk[log_count].emt = FECF_ERR_EID;
                    log.blk[log_count].emv[0] = 0x4E;
                    log.blk[log_count].emv[1] = 0x41;
                    log.blk[log_count].emv[2] = 0x53;
                    log.blk[log_count].emv[3] = 0x41;
                    log.blk[log_count++].em_len = 4;
                }
                #ifdef FECF_DEBUG
                    OS_printf("\t Calculated = 0x%04x \n\t Received   = 0x%04x \n", calc_fecf, tc_frame.tc_sec_trailer.fecf);
                #endif
                result = OS_ERROR;
            }
        }

    return result;
}

static uint16 Crypto_Calc_FECF(char* ingest, int len_ingest)
// Calculate the Frame Error Control Field (FECF), also known as a cyclic redundancy check (CRC)
{
    uint16 fecf = 0xFFFF;
    uint16 poly = 0x1021;	// TODO: This polynomial is (CRC-CCITT) for ESA testing, may not match standard protocol
    uint8 bit;
    uint8 c15;

    for (int i = 0; i <= len_ingest; i++)
    {	// Byte Logic
        for (int j = 0; j < 8; j++)
        {	// Bit Logic
            bit = ((ingest[i] >> (7 - j) & 1) == 1); 
            c15 = ((fecf >> 15 & 1) == 1); 
            fecf <<= 1;
            if (c15 ^ bit)
            {
                fecf ^= poly;
            }
        }
    }

    // Check if Testing
    if (badFECF == 1)
    {
        fecf++;
    }

    #ifdef FECF_DEBUG
        OS_printf(KCYN "Crypto_Calc_FECF: 0x%02x%02x%02x%02x%02x, len_ingest = %d\n" RESET, ingest[0], ingest[1], ingest[2], ingest[3], ingest[4], len_ingest);
        OS_printf(KCYN "0x" RESET);
        for (int x = 0; x < len_ingest; x++)
        {
            OS_printf(KCYN "%02x" RESET, ingest[x]);
        }
        OS_printf(KCYN "\n" RESET);
        OS_printf(KCYN "In Crypto_Calc_FECF! fecf = 0x%04x\n" RESET, fecf);
    #endif
    
    return fecf;
}

static uint16 Crypto_Calc_CRC16(char* data, int size)
{   // Code provided by ESA
    uint16 crc = 0xFFFF;

    for ( ; size > 0; size--)
    {  
        //OS_printf("*data = 0x%02x \n", (uint8) *data);
        crc = ((crc << 8) & 0xFF00) ^ crc16Table[(crc >> 8) ^ *data++];
    }
       
   return crc;
}

/*
** Key Management Services
*/
static int32 Crypto_Key_OTAR(void)
// The OTAR Rekeying procedure shall have the following Service Parameters:
//  a- Key ID of the Master Key (Integer, unmanaged)
//  b- Size of set of Upload Keys (Integer, managed)
//  c- Set of Upload Keys (Integer[Session Key]; managed)
// NOTE- The size of the session keys is mission specific.
//  a- Set of Key IDs of Upload Keys (Integer[Key IDs]; managed)
//  b- Set of Encrypted Upload Keys (Integer[Size of set of Key ID]; unmanaged)
//  c- Agreed Cryptographic Algorithm (managed)
{
    // Local variables
    SDLS_OTAR_t packet;
    int count = 0;
    int x = 0;
    int32 status = OS_SUCCESS;
    int pdu_keys = (sdls_frame.pdu.pdu_len - 30) / (2 + KEY_SIZE);

    gcry_cipher_hd_t tmp_hd;
    gcry_error_t gcry_error = GPG_ERR_NO_ERROR;

    // Master Key ID
    packet.mkid = (sdls_frame.pdu.data[0] << 8) | (sdls_frame.pdu.data[1]);

    if (packet.mkid >= 128)
    {
        report.af = 1;
        if (log_summary.rs > 0)
        {
            Crypto_increment((uint8*)&log_summary.num_se, 4);
            log_summary.rs--;
            log.blk[log_count].emt = MKID_INVALID_EID;
            log.blk[log_count].emv[0] = 0x4E;
            log.blk[log_count].emv[1] = 0x41;
            log.blk[log_count].emv[2] = 0x53;
            log.blk[log_count].emv[3] = 0x41;
            log.blk[log_count++].em_len = 4;
        }
        OS_printf(KRED "Error: MKID is not valid! \n" RESET);
        status = OS_ERROR;
        return status;
    }

    for (int count = 2; count < (2 + IV_SIZE); count++)
    {	// Initialization Vector
        packet.iv[count-2] = sdls_frame.pdu.data[count];
        //OS_printf("packet.iv[%d] = 0x%02x\n", count-2, packet.iv[count-2]);
    }
    
    count = sdls_frame.pdu.pdu_len - MAC_SIZE; 
    for (int w = 0; w < 16; w++)
    {	// MAC
        packet.mac[w] = sdls_frame.pdu.data[count + w];
        //OS_printf("packet.mac[%d] = 0x%02x\n", w, packet.mac[w]);
    }

    gcry_error = gcry_cipher_open(
        &(tmp_hd), 
        GCRY_CIPHER_AES256, 
        GCRY_CIPHER_MODE_GCM, 
        GCRY_CIPHER_CBC_MAC
    );
    if((gcry_error & GPG_ERR_CODE_MASK) != GPG_ERR_NO_ERROR)
    {
        OS_printf(KRED "ERROR: gcry_cipher_open error code %d\n" RESET,gcry_error & GPG_ERR_CODE_MASK);
        status = OS_ERROR;
        return status;
    }
    gcry_error = gcry_cipher_setkey(
        tmp_hd, 
        &(ek_ring[packet.mkid].value[0]), 
        KEY_SIZE
    );
    if((gcry_error & GPG_ERR_CODE_MASK) != GPG_ERR_NO_ERROR)
    {
        OS_printf(KRED "ERROR: gcry_cipher_setkey error code %d\n" RESET,gcry_error & GPG_ERR_CODE_MASK);
        status = OS_ERROR;
        return status;
    }
    gcry_error = gcry_cipher_setiv(
        tmp_hd, 
        &(packet.iv[0]), 
        IV_SIZE
    );
    if((gcry_error & GPG_ERR_CODE_MASK) != GPG_ERR_NO_ERROR)
    {
        OS_printf(KRED "ERROR: gcry_cipher_setiv error code %d\n" RESET,gcry_error & GPG_ERR_CODE_MASK);
        status = OS_ERROR;
        return status;
    }
    gcry_error = gcry_cipher_decrypt(
        tmp_hd,
        &(sdls_frame.pdu.data[14]),                     // plaintext output
        pdu_keys * (2 + KEY_SIZE),   			 		// length of data
        NULL,                                           // in place decryption
        0                                               // in data length
    );
    if((gcry_error & GPG_ERR_CODE_MASK) != GPG_ERR_NO_ERROR)
    {
        OS_printf(KRED "ERROR: gcry_cipher_decrypt error code %d\n" RESET,gcry_error & GPG_ERR_CODE_MASK);
        status = OS_ERROR;
        return status;
    }
    gcry_error = gcry_cipher_checktag(
        tmp_hd,
        &(packet.mac[0]),                               // tag input
        MAC_SIZE                                        // tag size
    );
    if((gcry_error & GPG_ERR_CODE_MASK) != GPG_ERR_NO_ERROR)
    {
        OS_printf(KRED "ERROR: gcry_cipher_checktag error code %d\n" RESET,gcry_error & GPG_ERR_CODE_MASK);
        status = OS_ERROR;
        return status;
    }
    gcry_cipher_close(tmp_hd);
    
    // Read in Decrypted Data
    for (int count = 14; x < pdu_keys; x++)
    {	// Encrypted Key Blocks
        packet.EKB[x].ekid = (sdls_frame.pdu.data[count] << 8) | (sdls_frame.pdu.data[count+1]);
        if (packet.EKB[x].ekid < 128)
        {
            report.af = 1;
            if (log_summary.rs > 0)
            {
                Crypto_increment((uint8*)&log_summary.num_se, 4);
                log_summary.rs--;
                log.blk[log_count].emt = OTAR_MK_ERR_EID;
                log.blk[log_count].emv[0] = 0x4E; // N
                log.blk[log_count].emv[1] = 0x41; // A
                log.blk[log_count].emv[2] = 0x53; // S
                log.blk[log_count].emv[3] = 0x41; // A
                log.blk[log_count++].em_len = 4;
            }
            OS_printf(KRED "Error: Cannot OTAR master key! \n" RESET);
            status = OS_ERROR;
        return status;
        }
        else
        {   
            count = count + 2;
            for (int y = count; y < (KEY_SIZE + count); y++)
            {	// Encrypted Key
                packet.EKB[x].ek[y-count] = sdls_frame.pdu.data[y];
                #ifdef SA_DEBUG
                    OS_printf("\t packet.EKB[%d].ek[%d] = 0x%02x\n", x, y-count, packet.EKB[x].ek[y-count]);
                #endif

                // Setup Key Ring
                ek_ring[packet.EKB[x].ekid].value[y - count] = sdls_frame.pdu.data[y];
            }
            count = count + KEY_SIZE;

            // Set state to PREACTIVE
            ek_ring[packet.EKB[x].ekid].key_state = KEY_PREACTIVE;
        }
    }

    #ifdef PDU_DEBUG
        OS_printf("Received %d keys via master key %d: \n", pdu_keys, packet.mkid);
        for (int x = 0; x < pdu_keys; x++)
        {
            OS_printf("%d) Key ID = %d, 0x", x+1, packet.EKB[x].ekid);
            for(int y = 0; y < KEY_SIZE; y++)
            {
                OS_printf("%02x", packet.EKB[x].ek[y]);
            }
            OS_printf("\n");
        } 
    #endif
    
    return OS_SUCCESS; 
}

static int32 Crypto_Key_update(uint8 state)
// Updates the state of the all keys in the received SDLS EP PDU
{	// Local variables
    SDLS_KEY_BLK_t packet;
    int count = 0;
    int pdu_keys = sdls_frame.pdu.pdu_len / 2;
    #ifdef PDU_DEBUG
        OS_printf("Keys ");
    #endif
    // Read in PDU
    for (int x = 0; x < pdu_keys; x++)
    {
        packet.kblk[x].kid = (sdls_frame.pdu.data[count] << 8) | (sdls_frame.pdu.data[count+1]);
        count = count + 2;
        #ifdef PDU_DEBUG
            if (x != (pdu_keys - 1))
            {
                OS_printf("%d, ", packet.kblk[x].kid);
            }
            else
            {
                OS_printf("and %d ", packet.kblk[x].kid);
            }
        #endif
    }
    #ifdef PDU_DEBUG
        OS_printf("changed to state ");
        switch (state)
        {
            case KEY_PREACTIVE:
                OS_printf("PREACTIVE. \n");
                break;
            case KEY_ACTIVE:
                OS_printf("ACTIVE. \n");
                break;
            case KEY_DEACTIVATED:
                OS_printf("DEACTIVATED. \n");
                break;
            case KEY_DESTROYED:
                OS_printf("DESTROYED. \n");
                break;
            case KEY_CORRUPTED:
                OS_printf("CORRUPTED. \n");
                break;
            default:
                OS_printf("ERROR. \n");
                break;
        }
    #endif
    // Update Key State
    for (int x = 0; x < pdu_keys; x++)
    {
        if (packet.kblk[x].kid < 128)
        {
            report.af = 1;
            if (log_summary.rs > 0)
            {
                Crypto_increment((uint8*)&log_summary.num_se, 4);
                log_summary.rs--;
                log.blk[log_count].emt = MKID_STATE_ERR_EID;
                log.blk[log_count].emv[0] = 0x4E;
                log.blk[log_count].emv[1] = 0x41;
                log.blk[log_count].emv[2] = 0x53;
                log.blk[log_count].emv[3] = 0x41;
                log.blk[log_count++].em_len = 4;
            }
            OS_printf(KRED "Error: MKID state cannot be changed! \n" RESET);
            // TODO: Exit
        }

        if (ek_ring[packet.kblk[x].kid].key_state == (state - 1))
        {
            ek_ring[packet.kblk[x].kid].key_state = state;
            #ifdef PDU_DEBUG
                //OS_printf("Key ID %d state changed to ", packet.kblk[x].kid);
            #endif
        }
        else 
        {
            if (log_summary.rs > 0)
            {
                Crypto_increment((uint8*)&log_summary.num_se, 4);
                log_summary.rs--;
                log.blk[log_count].emt = KEY_TRANSITION_ERR_EID;
                log.blk[log_count].emv[0] = 0x4E;
                log.blk[log_count].emv[1] = 0x41;
                log.blk[log_count].emv[2] = 0x53;
                log.blk[log_count].emv[3] = 0x41;
                log.blk[log_count++].em_len = 4;
            }
            OS_printf(KRED "Error: Key %d cannot transition to desired state! \n" RESET, packet.kblk[x].kid);
        }
    }
    return OS_SUCCESS; 
}

static int32 Crypto_Key_inventory(char* ingest)
{
    // Local variables
    SDLS_KEY_INVENTORY_t packet;
    int count = 0;
    uint16_t range = 0;

    // Read in PDU
    packet.kid_first = ((uint8)sdls_frame.pdu.data[count] << 8) | ((uint8)sdls_frame.pdu.data[count+1]);
    count = count + 2;
    packet.kid_last = ((uint8)sdls_frame.pdu.data[count] << 8) | ((uint8)sdls_frame.pdu.data[count+1]);
    count = count + 2;

    // Prepare for Reply
    range = packet.kid_last - packet.kid_first;
    sdls_frame.pdu.pdu_len = 2 + (range * (2 + 1));
    sdls_frame.hdr.pkt_length = sdls_frame.pdu.pdu_len + 9;
    count = Crypto_Prep_Reply(ingest, 128);
    ingest[count++] = (range & 0xFF00) >> 8;
    ingest[count++] = (range & 0x00FF);
    for (uint16_t x = packet.kid_first; x < packet.kid_last; x++)
    {   // Key ID
        ingest[count++] = (x & 0xFF00) >> 8;
        ingest[count++] = (x & 0x00FF);
        // Key State
        ingest[count++] = ek_ring[x].key_state;
    }
    return count;
}

static int32 Crypto_Key_verify(char* ingest)
{
    // Local variables
    SDLS_KEYV_CMD_t packet;
    int count = 0;
    int pdu_keys = sdls_frame.pdu.pdu_len / SDLS_KEYV_CMD_BLK_SIZE;

    gcry_error_t gcry_error = GPG_ERR_NO_ERROR;
    gcry_cipher_hd_t tmp_hd;
    uint8 iv_loc;

    //uint8 tmp_mac[MAC_SIZE];

    #ifdef PDU_DEBUG
        OS_printf("Crypto_Key_verify: Requested %d key(s) to verify \n", pdu_keys);
    #endif
    
    // Read in PDU
    for (int x = 0; x < pdu_keys; x++)
    {	
        // Key ID
        packet.blk[x].kid = ((uint8)sdls_frame.pdu.data[count] << 8) | ((uint8)sdls_frame.pdu.data[count+1]);
        count = count + 2;
        #ifdef PDU_DEBUG
            OS_printf("Crypto_Key_verify: Block %d Key ID is %d \n", x, packet.blk[x].kid);
        #endif
        // Key Challenge
        for (int y = 0; y < CHALLENGE_SIZE; y++)
        {
            packet.blk[x].challenge[y] = sdls_frame.pdu.data[count++];
        }
        #ifdef PDU_DEBUG
            OS_printf("\n");
        #endif
    }
    
    // Prepare for Reply
    sdls_frame.pdu.pdu_len = pdu_keys * (2 + IV_SIZE + CHALLENGE_SIZE + CHALLENGE_MAC_SIZE);
    sdls_frame.hdr.pkt_length = sdls_frame.pdu.pdu_len + 9;
    count = Crypto_Prep_Reply(ingest, 128);

    for (int x = 0; x < pdu_keys; x++)
    {   // Key ID
        ingest[count++] = (packet.blk[x].kid & 0xFF00) >> 8;
        ingest[count++] = (packet.blk[x].kid & 0x00FF);

        // Initialization Vector
        iv_loc = count;
        for (int y = 0; y < IV_SIZE; y++)
        {   
            ingest[count++] = tc_frame.tc_sec_header.iv[y];
        }
        ingest[count-1] = ingest[count-1] + x + 1;

        // Encrypt challenge 
        gcry_error = gcry_cipher_open(
            &(tmp_hd), 
            GCRY_CIPHER_AES256, 
            GCRY_CIPHER_MODE_GCM, 
            GCRY_CIPHER_CBC_MAC
        );
        if((gcry_error & GPG_ERR_CODE_MASK) != GPG_ERR_NO_ERROR)
        {
            OS_printf(KRED "ERROR: gcry_cipher_open error code %d\n" RESET,gcry_error & GPG_ERR_CODE_MASK);
        }
        gcry_error = gcry_cipher_setkey(
            tmp_hd, 
            &(ek_ring[packet.blk[x].kid].value[0]),
            KEY_SIZE
        );
        if((gcry_error & GPG_ERR_CODE_MASK) != GPG_ERR_NO_ERROR)
        {
            OS_printf(KRED "ERROR: gcry_cipher_setkey error code %d\n" RESET,gcry_error & GPG_ERR_CODE_MASK);
        }
        gcry_error = gcry_cipher_setiv(
            tmp_hd, 
            &(ingest[iv_loc]), 
            IV_SIZE
        );
        if((gcry_error & GPG_ERR_CODE_MASK) != GPG_ERR_NO_ERROR)
        {
            OS_printf(KRED "ERROR: gcry_cipher_setiv error code %d\n" RESET,gcry_error & GPG_ERR_CODE_MASK);
        }
        gcry_error = gcry_cipher_encrypt(
            tmp_hd,
            &(ingest[count]),                               // ciphertext output
            CHALLENGE_SIZE,			 		                // length of data
            &(packet.blk[x].challenge[0]),                  // plaintext input
            CHALLENGE_SIZE                                  // in data length
        );
        if((gcry_error & GPG_ERR_CODE_MASK) != GPG_ERR_NO_ERROR)
        {
            OS_printf(KRED "ERROR: gcry_cipher_encrypt error code %d\n" RESET,gcry_error & GPG_ERR_CODE_MASK);
        }
        count = count + CHALLENGE_SIZE; // Don't forget to increment count!
        
        gcry_error = gcry_cipher_gettag(
            tmp_hd,
            &(ingest[count]),                               // tag output
            CHALLENGE_MAC_SIZE                              // tag size
        );
        if((gcry_error & GPG_ERR_CODE_MASK) != GPG_ERR_NO_ERROR)
        {
            OS_printf(KRED "ERROR: gcry_cipher_gettag error code %d \n" RESET,gcry_error & GPG_ERR_CODE_MASK);
        }
        count = count + CHALLENGE_MAC_SIZE; // Don't forget to increment count!

        // Copy from tmp_mac into ingest
        //for( int y = 0; y < CHALLENGE_MAC_SIZE; y++)
        //{
        //    ingest[count++] = tmp_mac[y];
        //}
        gcry_cipher_close(tmp_hd);
    }

    #ifdef PDU_DEBUG
        OS_printf("Crypto_Key_verify: Response is %d bytes \n", count);
    #endif

    return count;
}


/*
** Security Association Management Services
*/
static int32 Crypto_SA_start(void)
{	
    // Local variables
    uint8 count = 0;
    uint16 spi = 0x0000;
    crypto_gvcid_t gvcid;

    // Read ingest
    spi = ((uint8)sdls_frame.pdu.data[0] << 8) | (uint8)sdls_frame.pdu.data[1];

    // Overwrite last PID
    sa[spi].lpid = (sdls_frame.pdu.type << 7) | (sdls_frame.pdu.uf << 6) | (sdls_frame.pdu.sg << 4) | sdls_frame.pdu.pid;

    // Check SPI exists and in 'Keyed' state
    if (spi < NUM_SA)
    {
        if (sa[spi].sa_state == SA_KEYED)
        {
            count = 2;

            for(int x = 0; x <= ((sdls_frame.pdu.pdu_len - 2) / 4); x++)
            {   // Read in GVCID
                gvcid.tfvn  = (sdls_frame.pdu.data[count] >> 4);
                gvcid.scid  = (sdls_frame.pdu.data[count] << 12)     |  
                              (sdls_frame.pdu.data[count + 1] << 4)  | 
                              (sdls_frame.pdu.data[count + 2] >> 4);
                gvcid.vcid  = (sdls_frame.pdu.data[count + 2] << 4)  |
                              (sdls_frame.pdu.data[count + 3] && 0x3F);
                gvcid.mapid = (sdls_frame.pdu.data[count + 3]);
                
                // TC
                if (gvcid.vcid != tc_frame.tc_header.vcid)
                {   // Clear all GVCIDs for provided SPI
                    if (gvcid.mapid == TYPE_TC)
                    {
                        for (int i = 0; i < NUM_GVCID; i++)
                        {   // TC
                            sa[spi].gvcid_tc_blk[x].tfvn  = 0;
                            sa[spi].gvcid_tc_blk[x].scid  = 0;
                            sa[spi].gvcid_tc_blk[x].vcid  = 0;
                            sa[spi].gvcid_tc_blk[x].mapid = 0;
                        }
                    }
                    // Write channel to SA
                    if (gvcid.mapid != TYPE_MAP)  
                    {   // TC
                        sa[spi].gvcid_tc_blk[gvcid.vcid].tfvn  = gvcid.tfvn;
                        sa[spi].gvcid_tc_blk[gvcid.vcid].scid  = gvcid.scid;
                        sa[spi].gvcid_tc_blk[gvcid.vcid].mapid = gvcid.mapid;
                    }
                    else
                    {
                        // TODO: Handle TYPE_MAP
                    }
                }
                // TM
                if (gvcid.vcid != tm_frame.tm_header.vcid)
                {   // Clear all GVCIDs for provided SPI
                    if (gvcid.mapid == TYPE_TM)
                    {
                        for (int i = 0; i < NUM_GVCID; i++)
                        {   // TM
                            sa[spi].gvcid_tm_blk[x].tfvn  = 0;
                            sa[spi].gvcid_tm_blk[x].scid  = 0;
                            sa[spi].gvcid_tm_blk[x].vcid  = 0;
                            sa[spi].gvcid_tm_blk[x].mapid = 0;
                        }
                    }
                    // Write channel to SA
                    if (gvcid.mapid != TYPE_MAP)  
                    {   // TM
                        sa[spi].gvcid_tm_blk[gvcid.vcid].tfvn  = gvcid.tfvn;
                        sa[spi].gvcid_tm_blk[gvcid.vcid].scid  = gvcid.scid;
                        sa[spi].gvcid_tm_blk[gvcid.vcid].vcid  = gvcid.vcid;
                        sa[spi].gvcid_tm_blk[gvcid.vcid].mapid = gvcid.mapid;
                    }
                    else
                    {
                        // TODO: Handle TYPE_MAP
                    }
                }                

                #ifdef PDU_DEBUG
                    OS_printf("SPI %d changed to OPERATIONAL state. \n", spi);
                    switch (gvcid.mapid)
                    {
                        case TYPE_TC:
                            OS_printf("Type TC, ");
                            break;
                        case TYPE_MAP:
                            OS_printf("Type MAP, ");
                            break;
                        case TYPE_TM:
                            OS_printf("Type TM, ");
                            break;
                        default:
                            OS_printf("Type Unknown, ");
                            break;
                    }
                #endif
            
                // Change to operational state
                sa[spi].sa_state = SA_OPERATIONAL;
            }
        }
        else
        {
            OS_printf(KRED "ERROR: SPI %d is not in the KEYED state.\n" RESET, spi);
        }
    }
    else
    {
        OS_printf(KRED "ERROR: SPI %d does not exist.\n" RESET, spi);
    }

    #ifdef DEBUG
        OS_printf("\t spi = %d \n", spi);
    #endif
    
    return OS_SUCCESS; 
}

static int32 Crypto_SA_stop(void)
{
    // Local variables
    uint16 spi = 0x0000;

    // Read ingest
    spi = ((uint8)sdls_frame.pdu.data[0] << 8) | (uint8)sdls_frame.pdu.data[1];
    OS_printf("spi = %d \n", spi);

    // Overwrite last PID
    sa[spi].lpid = (sdls_frame.pdu.type << 7) | (sdls_frame.pdu.uf << 6) | (sdls_frame.pdu.sg << 4) | sdls_frame.pdu.pid;

    // Check SPI exists and in 'Active' state
    if (spi < NUM_SA)
    {
        if (sa[spi].sa_state == SA_OPERATIONAL)
        {
            // Remove all GVC/GMAP IDs
            for (int x = 0; x < NUM_GVCID; x++)
            {   // TC
                sa[spi].gvcid_tc_blk[x].tfvn  = 0;
                sa[spi].gvcid_tc_blk[x].scid  = 0;
                sa[spi].gvcid_tc_blk[x].vcid  = 0;
                sa[spi].gvcid_tc_blk[x].mapid = 0;
                // TM
                sa[spi].gvcid_tm_blk[x].tfvn  = 0;
                sa[spi].gvcid_tm_blk[x].scid  = 0;
                sa[spi].gvcid_tm_blk[x].vcid  = 0;
                sa[spi].gvcid_tm_blk[x].mapid = 0;
            }
            
            // Change to operational state
            sa[spi].sa_state = SA_KEYED;
            #ifdef PDU_DEBUG
                OS_printf("SPI %d changed to KEYED state. \n", spi);
            #endif
        }
        else
        {
            OS_printf(KRED "ERROR: SPI %d is not in the OPERATIONAL state.\n" RESET, spi);
        }
    }
    else
    {
        OS_printf(KRED "ERROR: SPI %d does not exist.\n" RESET, spi);
    }

    #ifdef DEBUG
        OS_printf("\t spi = %d \n", spi);
    #endif

    return OS_SUCCESS; 
}

static int32 Crypto_SA_rekey(void)
{
    // Local variables
    uint16 spi = 0x0000;
    int count = 0;  
    int x = 0;  

    // Read ingest
    spi = ((uint8)sdls_frame.pdu.data[count] << 8) | (uint8)sdls_frame.pdu.data[count+1];
    count = count + 2;

    // Overwrite last PID
    sa[spi].lpid = (sdls_frame.pdu.type << 7) | (sdls_frame.pdu.uf << 6) | (sdls_frame.pdu.sg << 4) | sdls_frame.pdu.pid;

    // Check SPI exists and in 'Unkeyed' state
    if (spi < NUM_SA)
    {
        if (sa[spi].sa_state == SA_UNKEYED)
        {	// Encryption Key
            sa[spi].ekid = ((uint8)sdls_frame.pdu.data[count] << 8) | (uint8)sdls_frame.pdu.data[count+1];
            count = count + 2;

            // Authentication Key
            //sa[spi].akid = ((uint8)sdls_frame.pdu.data[count] << 8) | (uint8)sdls_frame.pdu.data[count+1];
            //count = count + 2;

            // Anti-Replay Counter
            #ifdef PDU_DEBUG
                OS_printf("SPI %d IV updated to: 0x", spi);
            #endif
            if (sa[spi].iv_len > 0)
            {   // Set IV - authenticated encryption
                for (x = count; x < (sa[spi].iv_len + count); x++)
                {
                    // TODO: Uncomment once fixed in ESA implementation
                    // TODO: Assuming this was fixed...
                    sa[spi].iv[x - count] = (uint8) sdls_frame.pdu.data[x];
                    #ifdef PDU_DEBUG
                        OS_printf("%02x", sdls_frame.pdu.data[x]);
                    #endif
                }
            }
            else
            {   // Set SN
                // TODO
            }
            #ifdef PDU_DEBUG
                OS_printf("\n");
            #endif

            // Change to keyed state
            sa[spi].sa_state = SA_KEYED;
            #ifdef PDU_DEBUG
                OS_printf("SPI %d changed to KEYED state with encrypted Key ID %d. \n", spi, sa[spi].ekid);
            #endif
        }
        else
        {
            OS_printf(KRED "ERROR: SPI %d is not in the UNKEYED state.\n" RESET, spi);
        }
    }
    else
    {
        OS_printf(KRED "ERROR: SPI %d does not exist.\n" RESET, spi);
    }

    #ifdef DEBUG
        OS_printf("\t spi  = %d \n", spi);
        OS_printf("\t ekid = %d \n", sa[spi].ekid);
        //OS_printf("\t akid = %d \n", sa[spi].akid);
    #endif

    return OS_SUCCESS; 
}

static int32 Crypto_SA_expire(void)
{
    // Local variables
    uint16 spi = 0x0000;

    // Read ingest
    spi = ((uint8)sdls_frame.pdu.data[0] << 8) | (uint8)sdls_frame.pdu.data[1];
    OS_printf("spi = %d \n", spi);

    // Overwrite last PID
    sa[spi].lpid = (sdls_frame.pdu.type << 7) | (sdls_frame.pdu.uf << 6) | (sdls_frame.pdu.sg << 4) | sdls_frame.pdu.pid;

    // Check SPI exists and in 'Keyed' state
    if (spi < NUM_SA)
    {
        if (sa[spi].sa_state == SA_KEYED)
        {	// Change to 'Unkeyed' state
            sa[spi].sa_state = SA_UNKEYED;
            #ifdef PDU_DEBUG
                OS_printf("SPI %d changed to UNKEYED state. \n", spi);
            #endif
        }
        else
        {
            OS_printf(KRED "ERROR: SPI %d is not in the KEYED state.\n" RESET, spi);
        }
    }
    else
    {
        OS_printf(KRED "ERROR: SPI %d does not exist.\n" RESET, spi);
    }

    return OS_SUCCESS; 
}

static int32 Crypto_SA_create(void)
{
    // Local variables
    uint8 count = 6;
    uint16 spi = 0x0000;

    // Read sdls_frame.pdu.data
    spi = ((uint8)sdls_frame.pdu.data[0] << 8) | (uint8)sdls_frame.pdu.data[1];
    OS_printf("spi = %d \n", spi);

    // Overwrite last PID
    sa[spi].lpid = (sdls_frame.pdu.type << 7) | (sdls_frame.pdu.uf << 6) | (sdls_frame.pdu.sg << 4) | sdls_frame.pdu.pid;

    // Write SA Configuration
    sa[spi].est = ((uint8)sdls_frame.pdu.data[2] & 0x80) >> 7;
    sa[spi].ast = ((uint8)sdls_frame.pdu.data[2] & 0x40) >> 6;
    sa[spi].shivf_len = ((uint8)sdls_frame.pdu.data[2] & 0x3F);
    sa[spi].shsnf_len = ((uint8)sdls_frame.pdu.data[3] & 0xFC) >> 2;
    sa[spi].shplf_len = ((uint8)sdls_frame.pdu.data[3] & 0x03);
    sa[spi].stmacf_len = ((uint8)sdls_frame.pdu.data[4]);
    sa[spi].ecs_len = ((uint8)sdls_frame.pdu.data[5]);
    for (int x = 0; x < sa[spi].ecs_len; x++)
    {
        sa[spi].ecs[x] = ((uint8)sdls_frame.pdu.data[count++]);
    }
    sa[spi].iv_len = ((uint8)sdls_frame.pdu.data[count++]);
    for (int x = 0; x < sa[spi].iv_len; x++)
    {
        sa[spi].iv[x] = ((uint8)sdls_frame.pdu.data[count++]);
    }
    sa[spi].acs_len = ((uint8)sdls_frame.pdu.data[count++]);
    for (int x = 0; x < sa[spi].acs_len; x++)
    {
        sa[spi].acs = ((uint8)sdls_frame.pdu.data[count++]);
    }
    sa[spi].abm_len = (uint8)((sdls_frame.pdu.data[count] << 8) | (sdls_frame.pdu.data[count+1]));
    count = count + 2;
    for (int x = 0; x < sa[spi].abm_len; x++)
    {
        sa[spi].abm[x] = ((uint8)sdls_frame.pdu.data[count++]);
    }
    sa[spi].arc_len = ((uint8)sdls_frame.pdu.data[count++]);
    for (int x = 0; x < sa[spi].arc_len; x++)
    {
        sa[spi].arc[x] = ((uint8)sdls_frame.pdu.data[count++]);
    }
    sa[spi].arcw_len = ((uint8)sdls_frame.pdu.data[count++]);
    for (int x = 0; x < sa[spi].arcw_len; x++)
    {
        sa[spi].arcw[x] = ((uint8)sdls_frame.pdu.data[count++]);
    }

    // TODO: Checks for valid data

    // Set state to unkeyed
    sa[spi].sa_state = SA_UNKEYED;

    #ifdef PDU_DEBUG
        Crypto_saPrint(&sa[spi]);
    #endif

    return OS_SUCCESS; 
}

static int32 Crypto_SA_delete(void)
{
    // Local variables
    uint16 spi = 0x0000;

    // Read ingest
    spi = ((uint8)sdls_frame.pdu.data[0] << 8) | (uint8)sdls_frame.pdu.data[1];
    OS_printf("spi = %d \n", spi);

    // Overwrite last PID
    sa[spi].lpid = (sdls_frame.pdu.type << 7) | (sdls_frame.pdu.uf << 6) | (sdls_frame.pdu.sg << 4) | sdls_frame.pdu.pid;

    // Check SPI exists and in 'Unkeyed' state
    if (spi < NUM_SA)
    {
        if (sa[spi].sa_state == SA_UNKEYED)
        {	// Change to 'None' state
            sa[spi].sa_state = SA_NONE;
            #ifdef PDU_DEBUG
                OS_printf("SPI %d changed to NONE state. \n", spi);
            #endif

            // TODO: Zero entire SA
        }
        else
        {
            OS_printf(KRED "ERROR: SPI %d is not in the UNKEYED state.\n" RESET, spi);
        }
    }
    else
    {
        OS_printf(KRED "ERROR: SPI %d does not exist.\n" RESET, spi);
    }

    return OS_SUCCESS; 
}

static int32 Crypto_SA_setARSN(void)
{
    // Local variables
    uint16 spi = 0x0000;

    // Read ingest
    spi = ((uint8)sdls_frame.pdu.data[0] << 8) | (uint8)sdls_frame.pdu.data[1];
    OS_printf("spi = %d \n", spi);

    // TODO: Check SA type (authenticated, encrypted, both) and set appropriately
    // TODO: Add more checks on bounds

    // Check SPI exists
    if (spi < NUM_SA)
    {
        #ifdef PDU_DEBUG
            OS_printf("SPI %d IV updated to: 0x", spi);
        #endif
        if (sa[spi].iv_len > 0)
        {   // Set IV - authenticated encryption
            for (int x = 0; x < IV_SIZE; x++)
            {
                sa[spi].iv[x] = (uint8) sdls_frame.pdu.data[x + 2];
                #ifdef PDU_DEBUG
                    OS_printf("%02x", sa[spi].iv[x]);
                #endif
            }
            Crypto_increment((uint8*)sa[spi].iv, IV_SIZE);
        }
        else
        {   // Set SN
            // TODO
        }
        #ifdef PDU_DEBUG
            OS_printf("\n");
        #endif
    }
    else
    {
        OS_printf("Crypto_SA_setARSN ERROR: SPI %d does not exist.\n", spi);
    }

    return OS_SUCCESS; 
}

static int32 Crypto_SA_setARSNW(void)
{
    // Local variables
    uint16 spi = 0x0000;

    // Read ingest
    spi = ((uint8)sdls_frame.pdu.data[0] << 8) | (uint8)sdls_frame.pdu.data[1];
    OS_printf("spi = %d \n", spi);

    // Check SPI exists
    if (spi < NUM_SA)
    {
        sa[spi].arcw_len = (uint8) sdls_frame.pdu.data[2];
        
        // Check for out of bounds
        if (sa[spi].arcw_len > (ARC_SIZE))
        {
            sa[spi].arcw_len = ARC_SIZE;    
        }

        for(int x = 0; x < sa[spi].arcw_len; x++)
        {
            sa[spi].arcw[x] = (uint8) sdls_frame.pdu.data[x+3];
        }
    }
    else
    {
        OS_printf("Crypto_SA_setARSNW ERROR: SPI %d does not exist.\n", spi);
    }

    return OS_SUCCESS; 
}

static int32 Crypto_SA_status(char* ingest)
{
    // Local variables
    int count = 0;
    uint16 spi = 0x0000;

    // Read ingest
    spi = ((uint8)sdls_frame.pdu.data[0] << 8) | (uint8)sdls_frame.pdu.data[1];
    OS_printf("spi = %d \n", spi);

    // Check SPI exists
    if (spi < NUM_SA)
    {
        // Prepare for Reply
        sdls_frame.pdu.pdu_len = 3;
        sdls_frame.hdr.pkt_length = sdls_frame.pdu.pdu_len + 9;
        count = Crypto_Prep_Reply(ingest, 128);
        // PDU
        ingest[count++] = (spi & 0xFF00) >> 8;
        ingest[count++] = (spi & 0x00FF);
        ingest[count++] = sa[spi].lpid;
    }
    else
    {
        OS_printf("Crypto_SA_status ERROR: SPI %d does not exist.\n", spi);
    }

    #ifdef SA_DEBUG
        Crypto_saPrint(&sa[spi]);
    #endif

    return count; 
}


/*
** Security Association Monitoring and Control
*/
static int32 Crypto_MC_ping(char* ingest)
{
    int count = 0;

    // Prepare for Reply
    sdls_frame.pdu.pdu_len = 0;
    sdls_frame.hdr.pkt_length = sdls_frame.pdu.pdu_len + 9;
    count = Crypto_Prep_Reply(ingest, 128);

    return count;
}

static int32 Crypto_MC_status(char* ingest)
{
    int count = 0;

    // TODO: Update log_summary.rs;

    // Prepare for Reply
    sdls_frame.pdu.pdu_len = 2; // 4
    sdls_frame.hdr.pkt_length = sdls_frame.pdu.pdu_len + 9;
    count = Crypto_Prep_Reply(ingest, 128);
    
    // PDU
    //ingest[count++] = (log_summary.num_se & 0xFF00) >> 8;
    ingest[count++] = (log_summary.num_se & 0x00FF);
    //ingest[count++] = (log_summary.rs & 0xFF00) >> 8;
    ingest[count++] = (log_summary.rs & 0x00FF);
    
    #ifdef PDU_DEBUG
        OS_printf("log_summary.num_se = 0x%02x \n",log_summary.num_se);
        OS_printf("log_summary.rs = 0x%02x \n",log_summary.rs);
    #endif

    return count;
}

static int32 Crypto_MC_dump(char* ingest)
{
    int count = 0;
    
    // Prepare for Reply
    sdls_frame.pdu.pdu_len = (log_count * 6);  // SDLS_MC_DUMP_RPLY_SIZE
    sdls_frame.hdr.pkt_length = sdls_frame.pdu.pdu_len + 9;
    count = Crypto_Prep_Reply(ingest, 128);

    // PDU
    for (int x = 0; x < log_count; x++)
    {
        ingest[count++] = log.blk[x].emt;
        //ingest[count++] = (log.blk[x].em_len & 0xFF00) >> 8;
        ingest[count++] = (log.blk[x].em_len & 0x00FF);
        for (int y = 0; y < EMV_SIZE; y++)
        {
            ingest[count++] = log.blk[x].emv[y];
        }
    }

    #ifdef PDU_DEBUG
        OS_printf("log_count = %d \n", log_count);
        OS_printf("log_summary.num_se = 0x%02x \n",log_summary.num_se);
        OS_printf("log_summary.rs = 0x%02x \n",log_summary.rs);
    #endif

    return count; 
}

static int32 Crypto_MC_erase(char* ingest)
{
    int count = 0;

    // Zero Logs
    for (int x = 0; x < LOG_SIZE; x++)
    {
        log.blk[x].emt = 0;
        log.blk[x].em_len = 0;
        for (int y = 0; y < EMV_SIZE; y++)
        {
            log.blk[x].emv[y] = 0;
        }
    }

    // Compute Summary
    log_count = 0;
    log_summary.num_se = 0;
    log_summary.rs = LOG_SIZE;

    // Prepare for Reply
    sdls_frame.pdu.pdu_len = 2; // 4
    sdls_frame.hdr.pkt_length = sdls_frame.pdu.pdu_len + 9;
    count = Crypto_Prep_Reply(ingest, 128);

    // PDU
    //ingest[count++] = (log_summary.num_se & 0xFF00) >> 8;
    ingest[count++] = (log_summary.num_se & 0x00FF);
    //ingest[count++] = (log_summary.rs & 0xFF00) >> 8;
    ingest[count++] = (log_summary.rs & 0x00FF);

    return count; 
}

static int32 Crypto_MC_selftest(char* ingest)
{
    uint8 count = 0;
    uint8 result = ST_OK;

    // TODO: Perform test

    // Prepare for Reply
    sdls_frame.pdu.pdu_len = 1;
    sdls_frame.hdr.pkt_length = sdls_frame.pdu.pdu_len + 9;
    count = Crypto_Prep_Reply(ingest, 128);

    ingest[count++] = result;
    
    return count; 
}

static int32 Crypto_SA_readARSN(char* ingest)
{
    uint8 count = 0;
    uint16 spi = 0x0000;

    // Read ingest
    spi = ((uint8)sdls_frame.pdu.data[0] << 8) | (uint8)sdls_frame.pdu.data[1];

    // Prepare for Reply
    sdls_frame.pdu.pdu_len = 2 + IV_SIZE;
    sdls_frame.hdr.pkt_length = sdls_frame.pdu.pdu_len + 9;
    count = Crypto_Prep_Reply(ingest, 128);

    // Write SPI to reply
    ingest[count++] = (spi & 0xFF00) >> 8;
    ingest[count++] = (spi & 0x00FF);

    if (sa[spi].iv_len > 0)
    {   // Set IV - authenticated encryption
        for (int x = 0; x < sa[spi].iv_len - 1; x++)
        {
            ingest[count++] = sa[spi].iv[x];
        }
        
        // TODO: Do we need this?
        if (sa[spi].iv[IV_SIZE - 1] > 0)
        {   // Adjust to report last received, not expected
            ingest[count++] = sa[spi].iv[IV_SIZE - 1] - 1;
        }
        else
        {   
            ingest[count++] = sa[spi].iv[IV_SIZE - 1];
        }
    }
    else
    {   
        // TODO
    }

    #ifdef PDU_DEBUG
        OS_printf("spi = %d \n", spi);
        if (sa[spi].iv_len > 0)
        {
            OS_printf("ARSN = 0x");
            for (int x = 0; x < sa[spi].iv_len; x++)
            {
                OS_printf("%02x", sa[spi].iv[x]);
            }
            OS_printf("\n");
        }
    #endif
    
    return count; 
}

static int32 Crypto_MC_resetalarm(void)
{   // Reset all alarm flags
    report.af = 0;
    report.bsnf = 0;
    report.bmacf = 0;
    report.ispif = 0;    
    return OS_SUCCESS; 
}

static int32 Crypto_User_IdleTrigger(char* ingest)
{
    uint8 count = 0;

    // Prepare for Reply
    sdls_frame.pdu.pdu_len = 0;
    sdls_frame.hdr.pkt_length = sdls_frame.pdu.pdu_len + 9;
    count = Crypto_Prep_Reply(ingest, 144);
    
    return count; 
}
                         
static int32 Crypto_User_BadSPI(void)
{
    // Toggle Bad Sequence Number
    if (badSPI == 0)
    {
        badSPI = 1;
    }   
    else
    {
        badSPI = 0;
    }
    
    return OS_SUCCESS; 
}

static int32 Crypto_User_BadMAC(void)
{
    // Toggle Bad MAC
    if (badMAC == 0)
    {
        badMAC = 1;
    }   
    else
    {
        badMAC = 0;
    }
    
    return OS_SUCCESS; 
}

static int32 Crypto_User_BadIV(void)
{
    // Toggle Bad MAC
    if (badIV == 0)
    {
        badIV = 1;
    }   
    else
    {
        badIV = 0;
    }
    
    return OS_SUCCESS; 
}

static int32 Crypto_User_BadFECF(void)
{
    // Toggle Bad FECF
    if (badFECF == 0)
    {
        badFECF = 1;
    }   
    else
    {
        badFECF = 0;
    }
    
    return OS_SUCCESS; 
}

static int32 Crypto_User_ModifyKey(void)
{
    // Local variables
    uint16 kid = ((uint8)sdls_frame.pdu.data[0] << 8) | ((uint8)sdls_frame.pdu.data[1]);
    uint8 mod = (uint8)sdls_frame.pdu.data[2];

    switch (mod)
    {
        case 1: // Invalidate Key
            ek_ring[kid].value[KEY_SIZE-1]++;
            OS_printf("Key %d value invalidated! \n", kid);
            break;
        case 2: // Modify key state
            ek_ring[kid].key_state = (uint8)sdls_frame.pdu.data[3] & 0x0F;
            OS_printf("Key %d state changed to %d! \n", kid, mod);
            break;
        default:
            // Error
            break;
    }
    
    return OS_SUCCESS; 
}

static int32 Crypto_User_ModifyActiveTM(void)
{
    tm_frame.tm_sec_header.spi = (uint8)sdls_frame.pdu.data[0];   
    return OS_SUCCESS; 
}

static int32 Crypto_User_ModifyVCID(void)
{
    tm_frame.tm_header.vcid = (uint8)sdls_frame.pdu.data[0];

    for (int i = 0; i < NUM_GVCID; i++)
    {
        for (int j = 0; j < NUM_SA; j++)
        {
            if (sa[i].gvcid_tm_blk[j].mapid == TYPE_TM)
            {
                if (sa[i].gvcid_tm_blk[j].vcid == tm_frame.tm_header.vcid)
                {
                    tm_frame.tm_sec_header.spi = i;
                    OS_printf("TM Frame SPI changed to %d \n",i);
                    break;
                }
            }
        }
    }

    return OS_SUCCESS;
}

/*
** Procedures Specifications
*/
static int32 Crypto_PDU(char* ingest)
{
    int32 status = OS_SUCCESS;
    
    switch (sdls_frame.pdu.type)
    {
        case 0:	// Command
            switch (sdls_frame.pdu.uf)
            {
                case 0:	// CCSDS Defined Command
                    switch (sdls_frame.pdu.sg)
                    {
                        case SG_KEY_MGMT:  // Key Management Procedure
                            switch (sdls_frame.pdu.pid)
                            {
                                case PID_OTAR:
                                    #ifdef PDU_DEBUG
                                        OS_printf(KGRN "Key OTAR\n" RESET);
                                    #endif
                                    status = Crypto_Key_OTAR();
                                    break;
                                case PID_KEY_ACTIVATION:
                                    #ifdef PDU_DEBUG
                                        OS_printf(KGRN "Key Activate\n" RESET);
                                    #endif
                                    status = Crypto_Key_update(KEY_ACTIVE);
                                    break;
                                case PID_KEY_DEACTIVATION:
                                    #ifdef PDU_DEBUG
                                        OS_printf(KGRN "Key Deactivate\n" RESET);
                                    #endif
                                    status = Crypto_Key_update(KEY_DEACTIVATED);
                                    break;
                                case PID_KEY_VERIFICATION:
                                    #ifdef PDU_DEBUG
                                        OS_printf(KGRN "Key Verify\n" RESET);
                                    #endif
                                    status = Crypto_Key_verify(ingest);
                                    break;
                                case PID_KEY_DESTRUCTION:
                                    #ifdef PDU_DEBUG
                                        OS_printf(KGRN "Key Destroy\n" RESET);
                                    #endif
                                    status = Crypto_Key_update(KEY_DESTROYED);
                                    break;
                                case PID_KEY_INVENTORY:
                                    #ifdef PDU_DEBUG
                                        OS_printf(KGRN "Key Inventory\n" RESET);
                                    #endif
                                    status = Crypto_Key_inventory(ingest);
                                    break;
                                default:
                                    OS_printf(KRED "Error: Crypto_PDU failed interpreting Key Management Procedure Identification Field! \n" RESET);
                                    break;
                            }
                            break;
                        case SG_SA_MGMT:  // Security Association Management Procedure
                            switch (sdls_frame.pdu.pid)
                            {
                                case PID_CREATE_SA:
                                    #ifdef PDU_DEBUG
                                        OS_printf(KGRN "SA Create\n" RESET); 
                                    #endif
                                    status = Crypto_SA_create();
                                    break;
                                case PID_DELETE_SA:
                                    #ifdef PDU_DEBUG
                                        OS_printf(KGRN "SA Delete\n" RESET);
                                    #endif
                                    status = Crypto_SA_delete();
                                    break;
                                case PID_SET_ARSNW:
                                    #ifdef PDU_DEBUG
                                        OS_printf(KGRN "SA setARSNW\n" RESET);
                                    #endif
                                    status = Crypto_SA_setARSNW();
                                    break;
                                case PID_REKEY_SA:
                                    #ifdef PDU_DEBUG
                                        OS_printf(KGRN "SA Rekey\n" RESET); 
                                    #endif
                                    status = Crypto_SA_rekey();
                                    break;
                                case PID_EXPIRE_SA:
                                    #ifdef PDU_DEBUG
                                        OS_printf(KGRN "SA Expire\n" RESET);
                                    #endif
                                    status = Crypto_SA_expire();
                                    break;
                                case PID_SET_ARSN:
                                    #ifdef PDU_DEBUG
                                        OS_printf(KGRN "SA SetARSN\n" RESET);
                                    #endif
                                    status = Crypto_SA_setARSN();
                                    break;
                                case PID_START_SA:
                                    #ifdef PDU_DEBUG
                                        OS_printf(KGRN "SA Start\n" RESET); 
                                    #endif
                                    status = Crypto_SA_start();
                                    break;
                                case PID_STOP_SA:
                                    #ifdef PDU_DEBUG
                                        OS_printf(KGRN "SA Stop\n" RESET);
                                    #endif
                                    status = Crypto_SA_stop();
                                    break;
                                case PID_READ_ARSN:
                                    #ifdef PDU_DEBUG
                                        OS_printf(KGRN "SA readARSN\n" RESET);
                                    #endif
                                    status = Crypto_SA_readARSN(ingest);
                                    break;
                                case PID_SA_STATUS:
                                    #ifdef PDU_DEBUG
                                        OS_printf(KGRN "SA Status\n" RESET);
                                    #endif
                                    status = Crypto_SA_status(ingest);
                                    break;
                                default:
                                    OS_printf(KRED "Error: Crypto_PDU failed interpreting SA Procedure Identification Field! \n" RESET);
                                    break;
                            }
                            break;
                        case SG_SEC_MON_CTRL:  // Security Monitoring & Control Procedure
                            switch (sdls_frame.pdu.pid)
                            {
                                case PID_PING:
                                    #ifdef PDU_DEBUG
                                        OS_printf(KGRN "MC Ping\n" RESET);
                                    #endif
                                    status = Crypto_MC_ping(ingest);
                                    break;
                                case PID_LOG_STATUS:
                                    #ifdef PDU_DEBUG
                                        OS_printf(KGRN "MC Status\n" RESET);
                                    #endif
                                    status = Crypto_MC_status(ingest);
                                    break;
                                case PID_DUMP_LOG:
                                    #ifdef PDU_DEBUG
                                        OS_printf(KGRN "MC Dump\n" RESET);
                                    #endif
                                    status = Crypto_MC_dump(ingest);
                                    break;
                                case PID_ERASE_LOG:
                                    #ifdef PDU_DEBUG
                                        OS_printf(KGRN "MC Erase\n" RESET);
                                    #endif
                                    status = Crypto_MC_erase(ingest);
                                    break;
                                case PID_SELF_TEST:
                                    #ifdef PDU_DEBUG
                                        OS_printf(KGRN "MC Selftest\n" RESET);
                                    #endif
                                    status = Crypto_MC_selftest(ingest);
                                    break;
                                case PID_ALARM_FLAG:
                                    #ifdef PDU_DEBUG
                                        OS_printf(KGRN "MC Reset Alarm\n" RESET);
                                    #endif
                                    status = Crypto_MC_resetalarm();
                                    break;
                                default:
                                    OS_printf(KRED "Error: Crypto_PDU failed interpreting MC Procedure Identification Field! \n" RESET);
                                    break;
                            }
                            break;
                        default: // ERROR
                            OS_printf(KRED "Error: Crypto_PDU failed interpreting Service Group! \n" RESET);
                            break;
                    }
                    break;
                    
                case 1: 	// User Defined Command
                    switch (sdls_frame.pdu.sg)
                    {
                        default:
                            switch (sdls_frame.pdu.pid)
                            {
                                case 0: // Idle Frame Trigger
                                    #ifdef PDU_DEBUG
                                        OS_printf(KMAG "User Idle Trigger\n" RESET);
                                    #endif
                                    status = Crypto_User_IdleTrigger(ingest);
                                    break;
                                case 1: // Toggle Bad SPI
                                    #ifdef PDU_DEBUG
                                        OS_printf(KMAG "User Toggle Bad SPI\n" RESET);
                                    #endif
                                    status = Crypto_User_BadSPI();
                                    break;
                                case 2: // Toggle Bad IV
                                    #ifdef PDU_DEBUG
                                        OS_printf(KMAG "User Toggle Bad IV\n" RESET);
                                    #endif
                                    status = Crypto_User_BadIV();\
                                    break;
                                case 3: // Toggle Bad MAC
                                    #ifdef PDU_DEBUG
                                        OS_printf(KMAG "User Toggle Bad MAC\n" RESET);
                                    #endif
                                    status = Crypto_User_BadMAC();
                                    break; 
                                case 4: // Toggle Bad FECF
                                    #ifdef PDU_DEBUG
                                        OS_printf(KMAG "User Toggle Bad FECF\n" RESET);
                                    #endif
                                    status = Crypto_User_BadFECF();
                                    break;
                                case 5: // Modify Key
                                    #ifdef PDU_DEBUG
                                        OS_printf(KMAG "User Modify Key\n" RESET);
                                    #endif
                                    status = Crypto_User_ModifyKey();
                                    break;
                                case 6: // Modify ActiveTM
                                    #ifdef PDU_DEBUG
                                        OS_printf(KMAG "User Modify Active TM\n" RESET);
                                    #endif
                                    status = Crypto_User_ModifyActiveTM();
                                    break;
                                case 7: // Modify TM VCID
                                    #ifdef PDU_DEBUG
                                        OS_printf(KMAG "User Modify VCID\n" RESET);
                                    #endif
                                    status = Crypto_User_ModifyVCID();
                                    break;
                                default:
                                    OS_printf(KRED "Error: Crypto_PDU received user defined command! \n" RESET);
                                    break;
                            }
                    }
                    break;
            }
            break;
            
        case 1:	// Reply
            OS_printf(KRED "Error: Crypto_PDU failed interpreting PDU Type!  Received a Reply!?! \n" RESET);
            break;
    }

    #ifdef CCSDS_DEBUG
        if (status > 0)
        {
            OS_printf(KMAG "CCSDS message put on software bus: 0x" RESET);
            for (int x = 0; x < status; x++)
            {
                OS_printf(KMAG "%02x " RESET, (uint8) ingest[x]);
            }
            OS_printf("\n");
        }
    #endif

    return status;
}

// Believe that, per protocol, this should accept an entire (partially completed) frame
// Refernece 3550b1 Figure 2-4
// Reference 3550b1 Section 3
// 3.2.2.3
/*
**The portion of the TC Transfer Frame contained in the TC ApplySecurity Payload
parameter includes the Security Header field. When the ApplySecurity Function is
called, the Security Header field is empty; i.e., the caller has not set any values in the
Security Header.
*/
int32 Crypto_TC_ApplySecurity(char** ingest, int* len_ingest)
{
    // Local Variables
    int32 status = OS_SUCCESS;
    unsigned char* tc_ingest = *ingest; // ptr to original buffer

    #ifdef DEBUG
        OS_printf(KYEL "\n----- Crypto_TC_ApplySecurity START -----\n" RESET);
    #endif

    #ifdef DEBUG
        // OS_printf("Printing TF Header....\n");
        OS_printf("TF Bytes received\n");
        OS_printf("DEBUG - ");
        for(int i=0; i <*len_ingest; i++)
        {
            OS_printf("%02X", ((uint8 *)&*tc_ingest)[i]);
        }
        OS_printf("\nPrinted %d bytes\n", *len_ingest);
    #endif

    TC_FramePrimaryHeader_t temp_tc_header;
    
    // Primary Header
    temp_tc_header.tfvn   = ((uint8)tc_ingest[0] & 0xC0) >> 6;
    temp_tc_header.bypass = ((uint8)tc_ingest[0] & 0x20) >> 5;
    temp_tc_header.cc     = ((uint8)tc_ingest[0] & 0x10) >> 4;
    temp_tc_header.spare  = ((uint8)tc_ingest[0] & 0x0C) >> 2;
    temp_tc_header.scid   = ((uint8)tc_ingest[0] & 0x03) << 8;
    temp_tc_header.scid   = tc_frame.tc_header.scid | (uint8)tc_ingest[1];
    temp_tc_header.vcid   = ((uint8)tc_ingest[2] & 0xFC) >> 2;
    temp_tc_header.fl     = ((uint8)tc_ingest[2] & 0x03) << 8;
    temp_tc_header.fl     = tc_frame.tc_header.fl | (uint8)tc_ingest[3];
    temp_tc_header.fsn	  = (uint8)tc_ingest[4];

    // Check if command frame flag set
    if (temp_tc_header.cc == 1)
    {
        /*
        ** CCSDS 232.0-B-3
        ** Section 6.3.1
        ** "Type-C frames do not have the Security Header and Security Trailer."
        */
        #ifdef TC_DEBUG
            OS_printf(KYEL "DEBUG - Received Control/Command frame - nothing to do.\n" RESET);
        #endif
    }

    // TODO: If command frame flag not set, need to know SDLS parameters
    // What is the best way to do this - copy in the entire SA for a channel?
    // Will need to know lengths of various parameters from the SA DB
    else
    {
        // ***TODO:
        // The ingest * we will receive will not have SDLS headers, we need a way
        // to query the SA with gvcid_tc_blk information and receive the SA back
        // Expect these calls will return an error is the SA is not usable

        // Query SA DB for active SA / SDLS paremeters
        // TODO: Likely API call
        // Consider making one call to pull the parameters, or individual API calls
        // TODO: Magic to make sure we're getting the correct SA.. 
        // currently SA DB allows for more than one
        SecurityAssociation_t temp_SA;
        int16_t spi = -1;
        for (int i=0; i < NUM_SA;i++)
        {
            if (sa[i].sa_state == SA_OPERATIONAL)
            {
                spi = i;
                //break; // Needs modified to ACTUALLY get the correct SA
                         // TODO: Likely API discussion
            }
        }

        // If no active SA found
        if (spi == -1)
        {
            OS_printf(KRED "Error: No operational SA found! \n" RESET);
            status = OS_ERROR;
        }
        // Found an active SA
        else
        {
            CFE_PSP_MemCpy(&temp_SA, &(sa[spi]), SA_SIZE);
            #ifdef TC_DEBUG
                OS_printf("Operational SA found at index %d.\n", spi);
            #endif
        }

        #ifdef TC_DEBUG
            OS_printf(KYEL "DEBUG - Received data frame.\n" RESET);
            OS_printf("Using SA 0x%04X settings.\n", spi);
        #endif

        // Verify SCID 
        if ((temp_tc_header.scid != temp_SA.gvcid_tc_blk[temp_tc_header.vcid].scid) \
            && (status == OS_SUCCESS))
        {
            OS_printf(KRED "Error: SCID incorrect! \n" RESET);
            status = OS_ERROR;
        }

        #ifdef TC_DEBUG
            OS_printf(KYEL "DEBUG - Printing local copy of SA Entry for current SPI.\n" RESET);
            Crypto_saPrint(&temp_SA);
        #endif

        // Verify SA SPI configurations
        // Check if SA is operational
        if ((temp_SA.sa_state != SA_OPERATIONAL) && (status == OS_SUCCESS))
        {
            OS_printf(KRED "Error: SA is not operational! \n" RESET);
            status = OS_ERROR;
        }
        // Check if this VCID exists / is mapped to TCs
        if ((temp_SA.gvcid_tc_blk[temp_tc_header.vcid].mapid != TYPE_TC) && (status = OS_SUCCESS))
        {	
            OS_printf(KRED "Error: SA does not map this VCID to TC! \n" RESET);
            status = OS_ERROR;
        }

        // If a check is invalid so far, can return
        if (status != OS_SUCCESS)
        {
            return status;
        }

        #ifdef SEC_FILL_DEBUG
            OS_printf("TF Bytes after security head/trail filled\n");
            OS_printf("DEBUG - ");
            for(int i=0; i <*len_ingest; i++)
            {
                OS_printf("%02X", ((uint8 *)ingest)[i]);
            }
            OS_printf("\nPrinted %d bytes\n", *len_ingest);
        #endif

        // Determine mode
        // Clear
        if ((temp_SA.est == 0) && (temp_SA.ast == 0))
        {
            #ifdef DEBUG
                OS_printf(KBLU "Creating a TC - CLEAR! \n" RESET);
            #endif

            // Total length of buffer to be malloced
            // Ingest length + segment header (1) + spi_index (2) + some variable length fields
            // TODO
            uint16 tc_buf_length = *len_ingest + 1 + 2 + temp_SA.shplf_len;

            #ifdef TC_DEBUG
                OS_printf(KYEL "DEBUG - Total TC Buffer to be malloced is: %d bytes\n" RESET, tc_buf_length);
                OS_printf(KYEL "\tlen of TF\t = %d\n" RESET, *len_ingest);
                OS_printf(KYEL "\tsegment hdr\t = 1\n" RESET);
                OS_printf(KYEL "\tspi len\t\t = 2\n" RESET);
                OS_printf(KYEL "\tsh pad len\t = %d\n" RESET, temp_SA.shplf_len);
            #endif

            // Accio buffer
            unsigned char * ptc_enc_buf;
            ptc_enc_buf = (unsigned char *)malloc(tc_buf_length * sizeof (unsigned char));
            CFE_PSP_MemSet(ptc_enc_buf, 0, tc_buf_length);

            // Copy original TF header
            CFE_PSP_MemCpy(ptc_enc_buf, &*tc_ingest, TC_FRAME_PRIMARYHEADER_SIZE);
            // Set new TF Header length
            // Recall: Length field is one minus total length per spec
            *(ptc_enc_buf+2) = ((*(ptc_enc_buf+2) & 0xFC) | (((tc_buf_length - 1) & (0x0300)) >> 8));
            *(ptc_enc_buf+3) = ((tc_buf_length - 1) & (0x00FF));

            #ifdef TC_DEBUG
                OS_printf(KYEL "Printing updated TF Header:\n\t");
                for (int i=0; i<TC_FRAME_PRIMARYHEADER_SIZE; i++)
                {
                    OS_printf("%02X", *(ptc_enc_buf+i));
                }
                // Recall: The buffer length is 1 greater than the field value set in the TCTF
                OS_printf("\n\tNew length is 0x%02X\n", tc_buf_length-1);
                OS_printf(RESET);
            #endif

            /*
            ** Start variable length fields
            */

            uint16_t index = TC_FRAME_PRIMARYHEADER_SIZE;
            // Set Secondary Header flags
            *(ptc_enc_buf + index) = 0xFF;
            index++;

            /*
            ** Begin Security Header Fields
            ** Reference CCSDS SDLP 3550b1 4.1.1.1.3
            */
            // Set SPI
            *(ptc_enc_buf + index) = ((spi & 0xFF00) >> 8);
            *(ptc_enc_buf + index + 1) = (spi & 0x00FF);
            index += 2;

            // Set anti-replay sequence number value
            /*
            ** 4.1.1.4.4 If authentication or authenticated encryption is not selected 
            ** for an SA, the Sequence Number field shall be zero octets in length.
            */

            // Determine if padding is needed
            // TODO: Likely SA API Call
            for (int i=0; i < temp_SA.shplf_len; i++)
            {
                // Temp fill Pad
                // TODO: What should pad be, set per channel/SA potentially?
                *(ptc_enc_buf + index) = 0x00;
                index++;
            }

            // Copy in original TF data
            // Copy original TF header
            CFE_PSP_MemCpy(ptc_enc_buf + index, &*(tc_ingest + TC_FRAME_PRIMARYHEADER_SIZE), *len_ingest-TC_FRAME_PRIMARYHEADER_SIZE);

            #ifdef TC_DEBUG
                OS_printf(KYEL "Printing new TC Frame:\n\t");
                for(int i=0; i < tc_buf_length; i++)
                {
                    OS_printf("%02X", *(ptc_enc_buf + i));
                }
                OS_printf("\n" RESET);
            #endif

        }
        // Authentication
        else if ((temp_SA.est == 0) && (temp_SA.ast == 1))
        {
            #ifdef DEBUG
                OS_printf(KBLU "Creating a TC - AUTHENTICATED! \n" RESET);
            #endif
            // TODO
        }
        // Encryption
        else if ((temp_SA.est == 1) && (temp_SA.ast == 0))
        {
            #ifdef DEBUG
                OS_printf(KBLU "Creating a TC - ENCRYPTED! \n" RESET);
            #endif
            // TODO
        }
        // Authenticated Encryption
        else if((temp_SA.est == 1) && (temp_SA.ast == 1))
        {
            #ifdef DEBUG
                OS_printf(KBLU "Creating a TC - AUTHENTICATED ENCRYPTION! \n" RESET);
            #endif

            // Total length of buffer to be malloced
            // Ingest length + segment header (1) + spi_index (2) + shivf_len (varies) + shsnf_len (varies) \
            // + shplf_len + arc_len + pad_size + stmacf_len 
            uint16 tc_buf_length = *len_ingest + 1 + 2 + temp_SA.shivf_len + temp_SA.shsnf_len + \
            temp_SA.shplf_len + temp_SA.arc_len + TC_PAD_SIZE + temp_SA.stmacf_len ;

            #ifdef TC_DEBUG
                OS_printf(KYEL "DEBUG - Total TC Buffer to be malloced is: %d bytes\n" RESET, tc_buf_length);
                OS_printf(KYEL "\tlen of TF\t = %d\n" RESET, *len_ingest);
                OS_printf(KYEL "\tsegment hdr\t = 1\n" RESET);
                OS_printf(KYEL "\tspi len\t\t = 2\n" RESET);
                OS_printf(KYEL "\tshivf_len\t = %d\n" RESET, temp_SA.shivf_len);
                OS_printf(KYEL "\tshsnf_len\t = %d\n" RESET, temp_SA.shsnf_len);
                OS_printf(KYEL "\tshplf len\t = %d\n" RESET, temp_SA.shplf_len);
                OS_printf(KYEL "\tarc_len\t\t = %d\n" RESET, temp_SA.arc_len);
                OS_printf(KYEL "\tpad_size\t = %d\n" RESET, TC_PAD_SIZE);
                OS_printf(KYEL "\tstmacf_len\t = %d\n" RESET, temp_SA.stmacf_len);
            #endif 

            // Accio buffer
            unsigned char * ptc_enc_buf;
            ptc_enc_buf = (unsigned char *)malloc(tc_buf_length * sizeof (unsigned char));
            CFE_PSP_MemSet(ptc_enc_buf, 0, tc_buf_length);

            // Copy original TF header
            CFE_PSP_MemCpy(ptc_enc_buf, &*tc_ingest, TC_FRAME_PRIMARYHEADER_SIZE);
            // Set new TF Header length
            // Recall: Length field is one minus total length per spec
            *(ptc_enc_buf+2) = ((*(ptc_enc_buf+2) & 0xFC) | (((tc_buf_length -1) & (0x0300)) >> 8));
            *(ptc_enc_buf+3) = ((tc_buf_length -1) & (0x00FF));

            #ifdef TC_DEBUG
                OS_printf(KYEL "Printing updated TF Header:\n\t");
                for (int i=0; i<TC_FRAME_PRIMARYHEADER_SIZE; i++)
                {
                    OS_printf("%02X", *(ptc_enc_buf+i));
                }
                // Recall: The buffer length is 1 greater than the field value set in the TCTF
                OS_printf("\n\tNew length is 0x%02X\n", tc_buf_length-1);
                OS_printf(RESET);
            #endif

            /*
            ** Start variable length fields
            */

            uint16_t index = TC_FRAME_PRIMARYHEADER_SIZE;
            // Set Secondary Header flags
            *(ptc_enc_buf + index) = 0xFF;
            index++;

            /*
            ** Begin Security Header Fields
            ** Reference CCSDS SDLP 3550b1 4.1.1.1.3
            */
            // Set SPI
            *(ptc_enc_buf + index) = ((spi & 0xFF00) >> 8);
            *(ptc_enc_buf + index + 1) = (spi & 0x00FF);
            index += 2;
            // Set Initialization Vector
            for (int i=0; i < temp_SA.shivf_len; i++)
            {
                // Temp fill IV
                *(ptc_enc_buf + index) = 0x55;
                index++;
            }
            // Set Sequence Number
            // TODO: Revisit wrt the note below
            // TODO: RESOLVE difference in arc_len and SN
            /*
            ** The Sequence Number field, if authentication or authenticated encryption is
            ** selected for an SA, shall contain the anti-replay sequence number, consisting of an integral
            ** number of octets.
            **
            ** Note: For systems which implement authenticated encryption using a simple
            ** incrementing counter as an initialization vector (i.e., as in counter-mode
            ** cryptographic algorithms), the Initialization Vector field of the Security Header
            ** may serve also as the Sequence Number. In this case, the Sequence Number
            ** field in the Security Header is zero octets in length.
            ^^ Reference: CCSDS SDLP 3550b1 4.1.1.4.2
            */
            //for (int i=0; i < temp_SA.shsnf_len; i++)
            for (int i=0; i < temp_SA.arc_len; i++)
            {
                // Temp fill SN
                *(ptc_enc_buf + index) = 0x66;
                index++;
            }
            // Set Pad Length
            // TODO TC_PAD_SIZE should probably be copied into the SA DB definition... right?
            //for (int i=0; i < temp_SA.shplf_len; i++)
            for (int i=0; i < TC_PAD_SIZE; i++)
            {
                // Temp fill PL
                *(ptc_enc_buf + index) = 0x77;
                index++;
            }

            // Copy in original TF data  -- except FECF
            // TODO: Need to know presence/absence of FECF represented
            // Recall: Frame length is set to length-1 per spec
            uint16_t original_data_len = (temp_tc_header.fl + 1) - TC_FRAME_PRIMARYHEADER_SIZE - TC_FRAME_FECF_SIZE;
            for(int i=0; i<original_data_len; i++)
            {
                *(ptc_enc_buf + index) = tc_ingest[TC_FRAME_PRIMARYHEADER_SIZE + i];
                index++;
            }

            // Set MAC Field
            for (int i=0; i < temp_SA.stmacf_len; i++)
            {
                // Temp fill MAC
                *(ptc_enc_buf + index) = 0x88;
                index++;
            }

            // Set FECF Field
            // TODO: Determine is FECF is even present
            // on this particular channel
            // TODO: Determine if needs recalculated
            for (int i=0; i < TC_FRAME_FECF_SIZE; i++)
            {
                // Copy over old FECF
                *(ptc_enc_buf + index) = tc_ingest[*len_ingest - (TC_FRAME_FECF_SIZE - i)];
                index++;
            }

            OS_printf(KRED "FINAL INDEX IS %d\n" RESET, index);

            #ifdef TC_DEBUG
                OS_printf(KYEL "Printing new TC Frame:\n\t");
                for(int i=0; i < tc_buf_length; i++)
                {
                    OS_printf("%02X", *(ptc_enc_buf + i));
                }
                OS_printf("\n" RESET);
            #endif

        }
    }

    #ifdef DEBUG
        OS_printf(KYEL "----- Crypto_TC_ApplySecurity END -----\n" RESET);
    #endif

    return status;
}

int32 Crypto_TC_ProcessSecurity( char* ingest, int* len_ingest)
// Loads the ingest frame into the global tc_frame while performing decryption
{
    // Local Variables
    int32 status = OS_SUCCESS;
    int x = 0;
    int y = 0;
    gcry_cipher_hd_t tmp_hd;
    gcry_error_t gcry_error = GPG_ERR_NO_ERROR;

    #ifdef DEBUG
        int len_ingest_i = *len_ingest;
        OS_printf(KYEL "\n----- Crypto_TC_ProcessSecurity START -----\n" RESET);
        OS_printf("Printing entire TC buffer:\n");
        for (int i=0; i < len_ingest_i; i++){
            OS_printf("%02X", (uint8)*(ingest+i));
        }
        OS_printf("\n");
    #endif

    // Primary Header
    tc_frame.tc_header.tfvn   = ((uint8)ingest[0] & 0xC0) >> 6;
    tc_frame.tc_header.bypass = ((uint8)ingest[0] & 0x20) >> 5;
    tc_frame.tc_header.cc     = ((uint8)ingest[0] & 0x10) >> 4;
    tc_frame.tc_header.spare  = ((uint8)ingest[0] & 0x0C) >> 2;
    tc_frame.tc_header.scid   = ((uint8)ingest[0] & 0x03) << 8;
    tc_frame.tc_header.scid   = tc_frame.tc_header.scid | (uint8)ingest[1];
    tc_frame.tc_header.vcid   = ((uint8)ingest[2] & 0xFC) >> 2;
    tc_frame.tc_header.fl     = ((uint8)ingest[2] & 0x03) << 8;
    tc_frame.tc_header.fl     = tc_frame.tc_header.fl | (uint8)ingest[3];
    tc_frame.tc_header.fsn	  = (uint8)ingest[4];

    // Security Header
    tc_frame.tc_sec_header.sh  = (uint8)ingest[5]; 
    tc_frame.tc_sec_header.spi = ((uint8)ingest[6] << 8) | (uint8)ingest[7];
    #ifdef TC_DEBUG
        OS_printf("vcid = %d \n", tc_frame.tc_header.vcid );
        OS_printf("spi  = %d \n", tc_frame.tc_sec_header.spi);
    #endif

    // Checks
    if (((uint8)ingest[18] == 0x0B) && ((uint8)ingest[19] == 0x00) && (((uint8)ingest[20] & 0xF0) == 0x40))
    {   
        // User packet check only used for ESA Testing!
    }
    else
    {   // Update last spi used
        report.lspiu = tc_frame.tc_sec_header.spi;

        // Verify 
        if (tc_frame.tc_header.scid != (SCID & 0x3FF))
        {
            OS_printf(KRED "Error: SCID incorrect! \n" RESET);
            status = OS_ERROR;
        }
        else
        {   
            switch (report.lspiu)
            {	// Invalid SPIs fall through to trigger flag in FSR
                case 0x0000:
                case 0xFFFF:
                    status = OS_ERROR;
                    report.ispif = 1;
                    OS_printf(KRED "Error: SPI invalid! \n" RESET);
                    break;
                default:
                    break;
            }
        }
        if ((report.lspiu > NUM_SA) && (status == OS_SUCCESS))
        {
            report.ispif = 1;
            OS_printf(KRED "Error: SPI value greater than NUM_SA! \n" RESET);
            status = OS_ERROR;
        }
        if (status == OS_SUCCESS)
        {
            if (sa[report.lspiu].gvcid_tc_blk[tc_frame.tc_header.vcid].mapid != TYPE_TC)
            {	
                OS_printf(KRED "Error: SA invalid type! \n" RESET);
                status = OS_ERROR;
            }
        }
        // TODO: I don't think this is needed.
        //if (status == OS_SUCCESS)
        //{
        //    if (sa[report.lspiu].gvcid_tc_blk[tc_frame.tc_header.vcid].vcid != tc_frame.tc_header.vcid)
        //    {	
        //        OS_printf(KRED "Error: VCID not mapped to provided SPI! \n" RESET);
        //        status = OS_ERROR;
        //    }
        //}
        if (status == OS_SUCCESS)
        {
            if (sa[report.lspiu].sa_state != SA_OPERATIONAL)
            {	
                OS_printf(KRED "Error: SA state not operational! \n" RESET);
                status = OS_ERROR;
            }
        }
        if (status != OS_SUCCESS)
        {
            report.af = 1;
            if (log_summary.rs > 0)
            {
                Crypto_increment((uint8*)&log_summary.num_se, 4);
                log_summary.rs--;
                log.blk[log_count].emt = SPI_INVALID_EID;
                log.blk[log_count].emv[0] = 0x4E;
                log.blk[log_count].emv[1] = 0x41;
                log.blk[log_count].emv[2] = 0x53;
                log.blk[log_count].emv[3] = 0x41;
                log.blk[log_count++].em_len = 4;
            }
            *len_ingest = 0;
            return status;
        }
    }
    
    // Determine mode via SPI
    if ((sa[tc_frame.tc_sec_header.spi].est == 1) && 
        (sa[tc_frame.tc_sec_header.spi].ast == 1))
    {	// Authenticated Encryption
        #ifdef DEBUG
            OS_printf(KBLU "ENCRYPTED TC Received!\n" RESET);
        #endif
        #ifdef TC_DEBUG
            OS_printf("IV: \n");
        #endif
        for (x = 8; x < (8 + IV_SIZE); x++)
        {
            tc_frame.tc_sec_header.iv[x-8] = (uint8)ingest[x];
            #ifdef TC_DEBUG
                OS_printf("\t iv[%d] = 0x%02x\n", x-8, tc_frame.tc_sec_header.iv[x-8]);
            #endif
        }
        report.snval = tc_frame.tc_sec_header.iv[IV_SIZE-1];

        #ifdef DEBUG
            OS_printf("\t tc_sec_header.iv[%d] = 0x%02x \n", IV_SIZE-1, tc_frame.tc_sec_header.iv[IV_SIZE-1]);
            OS_printf("\t sa[%d].iv[%d] = 0x%02x \n", tc_frame.tc_sec_header.spi, IV_SIZE-1, sa[tc_frame.tc_sec_header.spi].iv[IV_SIZE-1]);
        #endif

        // Check IV is in ARCW
        if ( Crypto_window(tc_frame.tc_sec_header.iv, sa[tc_frame.tc_sec_header.spi].iv, IV_SIZE, 
            sa[tc_frame.tc_sec_header.spi].arcw[sa[tc_frame.tc_sec_header.spi].arcw_len-1]) != OS_SUCCESS )
        {
            report.af = 1;
            report.bsnf = 1;
            if (log_summary.rs > 0)
            {
                Crypto_increment((uint8*)&log_summary.num_se, 4);
                log_summary.rs--;
                log.blk[log_count].emt = IV_WINDOW_ERR_EID;
                log.blk[log_count].emv[0] = 0x4E;
                log.blk[log_count].emv[1] = 0x41;
                log.blk[log_count].emv[2] = 0x53;
                log.blk[log_count].emv[3] = 0x41;
                log.blk[log_count++].em_len = 4;
            }
            OS_printf(KRED "Error: IV not in window! \n" RESET);
            #ifdef OCF_DEBUG
                Crypto_fsrPrint(&report);
            #endif
            status = OS_ERROR;
        }
        else 
        {
            if ( Crypto_compare_less_equal(tc_frame.tc_sec_header.iv, sa[tc_frame.tc_sec_header.spi].iv, IV_SIZE) == OS_SUCCESS )
            {   // Replay - IV value lower than expected
                report.af = 1;
                report.bsnf = 1;
                if (log_summary.rs > 0)
                {
                    Crypto_increment((uint8*)&log_summary.num_se, 4);
                    log_summary.rs--;
                    log.blk[log_count].emt = IV_REPLAY_ERR_EID;
                    log.blk[log_count].emv[0] = 0x4E;
                    log.blk[log_count].emv[1] = 0x41;
                    log.blk[log_count].emv[2] = 0x53;
                    log.blk[log_count].emv[3] = 0x41;
                    log.blk[log_count++].em_len = 4;
                }
                OS_printf(KRED "Error: IV replay! Value lower than expected! \n" RESET);
                #ifdef OCF_DEBUG
                    Crypto_fsrPrint(&report);
                #endif
                status = OS_ERROR;
            } 
            else
            {   // Adjust expected IV to acceptable received value
                for (int i = 0; i < (IV_SIZE); i++)
                {
                    sa[tc_frame.tc_sec_header.spi].iv[i] = tc_frame.tc_sec_header.iv[i];
                }
            }
        }
        
        if ( status == OS_ERROR )
        {   // Exit
            *len_ingest = 0;
            return status;
        }

        x = x + Crypto_Get_tcPayloadLength();

        #ifdef TC_DEBUG
            OS_printf("TC: \n"); 
            for (int temp = 0; temp < Crypto_Get_tcPayloadLength(); temp++)
            {	
                OS_printf("\t ingest[%d] = 0x%02x \n", temp, (uint8)ingest[temp+20]);
            }
        #endif

        // Security Trailer
        #ifdef TC_DEBUG
            OS_printf("MAC: \n");
        #endif
        for (y = x; y < (x + MAC_SIZE); y++)
        {
            tc_frame.tc_sec_trailer.mac[y-x]  = (uint8)ingest[y];
            #ifdef TC_DEBUG
                OS_printf("\t mac[%d] = 0x%02x\n", y-x, tc_frame.tc_sec_trailer.mac[y-x]);
            #endif
        }
        x = x + MAC_SIZE;

        // FECF
        tc_frame.tc_fecf.fecf = ((uint8)ingest[x] << 8) | ((uint8)ingest[x+1]);
        Crypto_FECF(tc_frame.tc_fecf.fecf, ingest, (tc_frame.tc_header.fl - 2));

        // Initialize the key
        //itc_gcm128_init(&sa[tc_frame.tc_sec_header.spi].gcm_ctx, (const unsigned char*) &ek_ring[sa[tc_frame.tc_sec_header.spi].ekid]);

        gcry_error = gcry_cipher_open(
            &(tmp_hd),
            GCRY_CIPHER_AES256, 
            GCRY_CIPHER_MODE_GCM, 
            GCRY_CIPHER_CBC_MAC
        );
        if((gcry_error & GPG_ERR_CODE_MASK) != GPG_ERR_NO_ERROR)
        {
            OS_printf(KRED "ERROR: gcry_cipher_open error code %d\n" RESET,gcry_error & GPG_ERR_CODE_MASK);
            status = OS_ERROR;
            return status;
        }
        #ifdef DEBUG
            OS_printf("Key ID = %d, 0x", sa[tc_frame.tc_sec_header.spi].ekid);
            for(int y = 0; y < KEY_SIZE; y++)
            {
                OS_printf("%02x", ek_ring[sa[tc_frame.tc_sec_header.spi].ekid].value[y]);
            }
            OS_printf("\n");
        #endif
        gcry_error = gcry_cipher_setkey(
            tmp_hd,
            &(ek_ring[sa[tc_frame.tc_sec_header.spi].ekid].value[0]), 
            KEY_SIZE
        );
        if((gcry_error & GPG_ERR_CODE_MASK) != GPG_ERR_NO_ERROR)
        {
            OS_printf(KRED "ERROR: gcry_cipher_setkey error code %d\n" RESET,gcry_error & GPG_ERR_CODE_MASK);
            status = OS_ERROR;
            return status;
        }
        gcry_error = gcry_cipher_setiv(
            tmp_hd,
            &(sa[tc_frame.tc_sec_header.spi].iv[0]), 
            sa[tc_frame.tc_sec_header.spi].iv_len
        );
        if((gcry_error & GPG_ERR_CODE_MASK) != GPG_ERR_NO_ERROR)
        {
            OS_printf(KRED "ERROR: gcry_cipher_setiv error code %d\n" RESET,gcry_error & GPG_ERR_CODE_MASK);
            status = OS_ERROR;
            return status;
        }
        #ifdef MAC_DEBUG
            OS_printf("AAD = 0x");
        #endif
        // Prepare additional authenticated data (AAD)
        for (y = 0; y < sa[tc_frame.tc_sec_header.spi].abm_len; y++)
        {
            ingest[y] = (uint8) ((uint8)ingest[y] & (uint8)sa[tc_frame.tc_sec_header.spi].abm[y]);
            #ifdef MAC_DEBUG
                OS_printf("%02x", (uint8) ingest[y]);
            #endif
        }
        #ifdef MAC_DEBUG
            OS_printf("\n");
        #endif

        gcry_error = gcry_cipher_decrypt(
            tmp_hd, 
            &(tc_frame.tc_pdu[0]),                          // plaintext output
            Crypto_Get_tcPayloadLength(),			 		// length of data
            &(ingest[20]),                                  // ciphertext input
            Crypto_Get_tcPayloadLength()                    // in data length
        );
        if((gcry_error & GPG_ERR_CODE_MASK) != GPG_ERR_NO_ERROR)
        {
            OS_printf(KRED "ERROR: gcry_cipher_decrypt error code %d\n" RESET,gcry_error & GPG_ERR_CODE_MASK);
            status = OS_ERROR;
            return status;
        }
        gcry_error = gcry_cipher_checktag(
            tmp_hd, 
            &(tc_frame.tc_sec_trailer.mac[0]),              // tag input
            MAC_SIZE                                        // tag size
        );
        if((gcry_error & GPG_ERR_CODE_MASK) != GPG_ERR_NO_ERROR)
        {
            OS_printf(KRED "ERROR: gcry_cipher_checktag error code %d\n" RESET,gcry_error & GPG_ERR_CODE_MASK);
            
            OS_printf("Actual MAC   = 0x");
            for (int z = 0; z < MAC_SIZE; z++)
            {
                OS_printf("%02x",tc_frame.tc_sec_trailer.mac[z]);
            }
            OS_printf("\n");
            
            gcry_error = gcry_cipher_gettag(
                tmp_hd,
                &(tc_frame.tc_sec_trailer.mac[0]),          // tag output
                MAC_SIZE                                    // tag size
            );
            if((gcry_error & GPG_ERR_CODE_MASK) != GPG_ERR_NO_ERROR)
            {
                OS_printf(KRED "ERROR: gcry_cipher_checktag error code %d\n" RESET,gcry_error & GPG_ERR_CODE_MASK);
            }

            OS_printf("Expected MAC = 0x");
            for (int z = 0; z < MAC_SIZE; z++)
            {
                OS_printf("%02x",tc_frame.tc_sec_trailer.mac[z]);
            }
            OS_printf("\n");
            status = OS_ERROR;
            report.bmacf = 1;
            #ifdef OCF_DEBUG
                Crypto_fsrPrint(&report);
            #endif
            return status;
        }
        gcry_cipher_close(tmp_hd);
        
        // Increment the IV for next time
        #ifdef INCREMENT
            Crypto_increment(sa[tc_frame.tc_sec_header.spi].iv, IV_SIZE);
        #endif
    }
    else
    {	// Clear
        #ifdef DEBUG
            OS_printf(KBLU "CLEAR TC Received!\n" RESET);
        #endif

        // #ifdef TC_DEBUG
        //     uint8 sa_iv_len = sa[tc_frame.tc_sec_header.spi].iv_len; // John - shivf, or iv_len?
        //     OS_printf("Fetching IV Size: %d\n", sa_iv_len); 
        //     if (sa_iv_len > 0){
        //         OS_printf("Printing IV: \n");
        //         for (int i = 0; i < sa[tc_frame.tc_sec_header.spi].iv_len; i++)
        //         {
        //             OS_printf("%02x",tc_frame.tc_sec_header.iv[i]);
        //         }
        //         OS_printf("\n");
        //     }
        //     else{
        //         OS_printf("IV is 0 length for this SA.\n");
        //     }
        // #endif

        for (y = 10; y <= (tc_frame.tc_header.fl - 2); y++)
        {	
            tc_frame.tc_pdu[y - 10] = (uint8)ingest[y]; 
        }
        // FECF
        tc_frame.tc_fecf.fecf = ((uint8)ingest[y] << 8) | ((uint8)ingest[y+1]);
        Crypto_FECF((int) tc_frame.tc_fecf.fecf, ingest, (tc_frame.tc_header.fl - 2));
    }
    
    #ifdef TC_DEBUG
        Crypto_tcPrint(&tc_frame);
        OS_printf("\t\t Overall TF length is: %d + 1 = %d bytes\n", tc_frame.tc_header.fl, (tc_frame.tc_header.fl+1));
        OS_printf("\t\t TODO: Adjust to print payload only!\n");
        OS_printf("\t\t Decrypted TC bytes:  0x");
        for(int i=0; i < tc_frame.tc_header.fl; i++){
            OS_printf("%02X",((uint8 *)&tc_frame)[i]);
        }
        OS_printf("\n");
    #endif

    // Zero ingest
    for (x = 0; x < *len_ingest; x++)
    {
        ingest[x] = 0;
    }
    
    if ((tc_frame.tc_pdu[0] == 0x18) && (tc_frame.tc_pdu[1] == 0x80))	
    // Crypto Lib Application ID
    {
        #ifdef DEBUG
            OS_printf(KGRN "Received SDLS command: " RESET);
        #endif
        // CCSDS Header
        sdls_frame.hdr.pvn  	  = (tc_frame.tc_pdu[0] & 0xE0) >> 5;
        sdls_frame.hdr.type 	  = (tc_frame.tc_pdu[0] & 0x10) >> 4;
        sdls_frame.hdr.shdr 	  = (tc_frame.tc_pdu[0] & 0x08) >> 3;
        sdls_frame.hdr.appID      = ((tc_frame.tc_pdu[0] & 0x07) << 8) | tc_frame.tc_pdu[1];
        sdls_frame.hdr.seq  	  = (tc_frame.tc_pdu[2] & 0xC0) >> 6;
        sdls_frame.hdr.pktid      = ((tc_frame.tc_pdu[2] & 0x3F) << 8) | tc_frame.tc_pdu[3];
        sdls_frame.hdr.pkt_length = (tc_frame.tc_pdu[4] << 8) | tc_frame.tc_pdu[5];
        
        // CCSDS PUS
        sdls_frame.pus.shf		  = (tc_frame.tc_pdu[6] & 0x80) >> 7;
        sdls_frame.pus.pusv		  = (tc_frame.tc_pdu[6] & 0x70) >> 4;
        sdls_frame.pus.ack		  = (tc_frame.tc_pdu[6] & 0x0F);
        sdls_frame.pus.st		  = tc_frame.tc_pdu[7];
        sdls_frame.pus.sst		  = tc_frame.tc_pdu[8];
        sdls_frame.pus.sid		  = (tc_frame.tc_pdu[9] & 0xF0) >> 4;
        sdls_frame.pus.spare	  = (tc_frame.tc_pdu[9] & 0x0F);
        
        // SDLS TLV PDU
        sdls_frame.pdu.type 	  = (tc_frame.tc_pdu[10] & 0x80) >> 7;
        sdls_frame.pdu.uf   	  = (tc_frame.tc_pdu[10] & 0x40) >> 6;
        sdls_frame.pdu.sg   	  = (tc_frame.tc_pdu[10] & 0x30) >> 4;
        sdls_frame.pdu.pid  	  = (tc_frame.tc_pdu[10] & 0x0F);
        sdls_frame.pdu.pdu_len 	  = (tc_frame.tc_pdu[11] << 8) | tc_frame.tc_pdu[12];
        for (x = 13; x < (13 + sdls_frame.hdr.pkt_length); x++)
        {
            sdls_frame.pdu.data[x-13] = tc_frame.tc_pdu[x]; 
        }
        
        #ifdef CCSDS_DEBUG
            Crypto_ccsdsPrint(&sdls_frame); 
        #endif

        // Determine type of PDU
        *len_ingest = Crypto_PDU(ingest);
    }
    else
    {	// CCSDS Pass-through
        #ifdef DEBUG
            OS_printf(KGRN "CCSDS Pass-through \n" RESET);
        #endif
        // TODO: Remove PUS Header
        for (x = 0; x < (tc_frame.tc_header.fl - 11); x++)
        {
            ingest[x] = tc_frame.tc_pdu[x];
            #ifdef CCSDS_DEBUG
                OS_printf("tc_frame.tc_pdu[%d] = 0x%02x\n", x, tc_frame.tc_pdu[x]);
            #endif
        }
        *len_ingest = x;
    }

    #ifdef OCF_DEBUG
        Crypto_fsrPrint(&report);
    #endif
    
    #ifdef DEBUG
        OS_printf(KYEL "----- Crypto_TC_ProcessSecurity END -----\n" RESET);
    #endif

    return status;
}


int32 Crypto_TM_ApplySecurity( char* ingest, int* len_ingest)
// Accepts CCSDS message in ingest, and packs into TM before encryption
{
    int32 status = ITC_GCM128_SUCCESS;
    int count = 0;
    int pdu_loc = 0;
    int pdu_len = *len_ingest - TM_MIN_SIZE;
    int pad_len = 0;
    int mac_loc = 0;
    int fecf_loc = 0;
    uint8 tempTM[TM_SIZE];
    int x = 0;
    int y = 0;
    uint8 aad[20];
    uint16 spi = tm_frame.tm_sec_header.spi;
    uint16 spp_crc = 0x0000;

    gcry_cipher_hd_t tmp_hd;
    gcry_error_t gcry_error = GPG_ERR_NO_ERROR;
    CFE_PSP_MemSet(&tempTM, 0, TM_SIZE);
    
    #ifdef DEBUG
        OS_printf(KYEL "\n----- Crypto_TM_ApplySecurity START -----\n" RESET);
    #endif

    // Check for idle frame trigger
    if (((uint8)ingest[0] == 0x08) && ((uint8)ingest[1] == 0x90))
    {   // Zero ingest
        for (x = 0; x < *len_ingest; x++)
        {
            ingest[x] = 0;
        }
        // Update TM First Header Pointer
        tm_frame.tm_header.fhp = 0xFE;
    }   
    else
    {   // Update the length of the ingest from the CCSDS header
        *len_ingest = (ingest[4] << 8) | ingest[5];
        ingest[5] = ingest[5] - 5;
        // Remove outgoing secondary space packet header flag
        ingest[0] = 0x00;
        // Change sequence flags to 0xFFFF
        ingest[2] = 0xFF;
        ingest[3] = 0xFF;
        // Add 2 bytes of CRC to space packet
        spp_crc = Crypto_Calc_CRC16((char*) ingest, *len_ingest);
        ingest[*len_ingest] = (spp_crc & 0xFF00) >> 8;
        ingest[*len_ingest+1] = (spp_crc & 0x00FF);
        *len_ingest = *len_ingest + 2;
        // Update TM First Header Pointer
        tm_frame.tm_header.fhp = tm_offset;
        #ifdef TM_DEBUG
            OS_printf("tm_offset = %d \n", tm_offset);
        #endif
    }             

    // Update Current Telemetry Frame in Memory
        // Counters
        tm_frame.tm_header.mcfc++;
        tm_frame.tm_header.vcfc++;
        // Operational Control Field 
        Crypto_TM_updateOCF();
        // Payload Data Unit
        Crypto_TM_updatePDU(ingest, *len_ingest);

    // Check test flags
        if (badSPI == 1)
        {
            tm_frame.tm_sec_header.spi++; 
        }
        if (badIV == 1)
        {
            sa[tm_frame.tm_sec_header.spi].iv[IV_SIZE-1]++;
        }
        if (badMAC == 1)
        {
            tm_frame.tm_sec_trailer.mac[MAC_SIZE-1]++;
        }

    // Initialize the temporary TM frame
        // Header
        tempTM[count++] = (uint8) ((tm_frame.tm_header.tfvn << 6) | ((tm_frame.tm_header.scid & 0x3F0) >> 4));
        tempTM[count++] = (uint8) (((tm_frame.tm_header.scid & 0x00F) << 4) | (tm_frame.tm_header.vcid << 1) | (tm_frame.tm_header.ocff));
        tempTM[count++] = (uint8) (tm_frame.tm_header.mcfc);
        tempTM[count++] = (uint8) (tm_frame.tm_header.vcfc);
        tempTM[count++] = (uint8) ((tm_frame.tm_header.tfsh << 7) | (tm_frame.tm_header.sf << 6) | (tm_frame.tm_header.pof << 5) | (tm_frame.tm_header.slid << 3) | ((tm_frame.tm_header.fhp & 0x700) >> 8));
        tempTM[count++] = (uint8) (tm_frame.tm_header.fhp & 0x0FF);
        //	tempTM[count++] = (uint8) ((tm_frame.tm_header.tfshvn << 6) | tm_frame.tm_header.tfshlen);
        // Security Header
        tempTM[count++] = (uint8) ((spi & 0xFF00) >> 8);
        tempTM[count++] = (uint8) ((spi & 0x00FF));
        CFE_PSP_MemCpy(tm_frame.tm_sec_header.iv, sa[spi].iv, IV_SIZE);

        // Padding Length
            pad_len = Crypto_Get_tmLength(*len_ingest) - TM_MIN_SIZE + IV_SIZE + TM_PAD_SIZE - *len_ingest;
        
        // Only add IV for authenticated encryption 
        if ((sa[spi].est == 1) && 
            (sa[spi].ast == 1))		
        {	// Initialization Vector
            #ifdef INCREMENT
                Crypto_increment(sa[tm_frame.tm_sec_header.spi].iv, IV_SIZE);
            #endif
            if ((sa[spi].est == 1) || (sa[spi].ast == 1))
            {	for (x = 0; x < IV_SIZE; x++)
                {
                    tempTM[count++] = sa[tm_frame.tm_sec_header.spi].iv[x];
                }
            }
            pdu_loc = count;
            pad_len = pad_len - IV_SIZE - TM_PAD_SIZE + OCF_SIZE;
            pdu_len = *len_ingest + pad_len;
        }
        else	
        {	// Include padding length bytes - hard coded per ESA testing
            tempTM[count++] = 0x00;  // pad_len >> 8; 
            tempTM[count++] = 0x1A;  // pad_len
            pdu_loc = count;
            pdu_len = *len_ingest + pad_len;
        }
        
        // Payload Data Unit
        for (x = 0; x < (pdu_len); x++)	
        {
            tempTM[count++] = (uint8) tm_frame.tm_pdu[x];
        }
        // Message Authentication Code
        mac_loc = count;
        for (x = 0; x < MAC_SIZE; x++)
        {
            tempTM[count++] = 0x00;
        }
        // Operational Control Field
        for (x = 0; x < OCF_SIZE; x++)
        {
            tempTM[count++] = (uint8) tm_frame.tm_sec_trailer.ocf[x];
        }
        // Frame Error Control Field
        fecf_loc = count;
        tm_frame.tm_sec_trailer.fecf = Crypto_Calc_FECF((char*) tempTM, count);	
        tempTM[count++] = (uint8) ((tm_frame.tm_sec_trailer.fecf & 0xFF00) >> 8);
        tempTM[count++] = (uint8) (tm_frame.tm_sec_trailer.fecf & 0x00FF);

    // Determine Mode
        // Clear
        if ((sa[spi].est == 0) && 
            (sa[spi].ast == 0))
        {
            #ifdef DEBUG
                OS_printf(KBLU "Creating a TM - CLEAR! \n" RESET);
            #endif
            // Copy temporary frame to ingest
            CFE_PSP_MemCpy(ingest, tempTM, count);
        }
        // Authenticated Encryption
        else if ((sa[spi].est == 1) && 
                 (sa[spi].ast == 1))
        {
            #ifdef DEBUG
                OS_printf(KBLU "Creating a TM - AUTHENTICATED ENCRYPTION! \n" RESET);
            #endif

            // Copy TM to ingest
            CFE_PSP_MemCpy(ingest, tempTM, pdu_loc);
            
            #ifdef MAC_DEBUG
                OS_printf("AAD = 0x");
            #endif
            // Prepare additional authenticated data
            for (y = 0; y < sa[spi].abm_len; y++)
            {
                aad[y] = ingest[y] & sa[spi].abm[y];
                #ifdef MAC_DEBUG
                    OS_printf("%02x", aad[y]);
                #endif
            }
            #ifdef MAC_DEBUG
                OS_printf("\n");
            #endif

            gcry_error = gcry_cipher_open(
                &(tmp_hd), 
                GCRY_CIPHER_AES256, 
                GCRY_CIPHER_MODE_GCM, 
                GCRY_CIPHER_CBC_MAC
            );
            if((gcry_error & GPG_ERR_CODE_MASK) != GPG_ERR_NO_ERROR)
            {
                OS_printf(KRED "ERROR: gcry_cipher_open error code %d\n" RESET,gcry_error & GPG_ERR_CODE_MASK);
                status = OS_ERROR;
                return status;
            }
            gcry_error = gcry_cipher_setkey(
                tmp_hd, 
                &(ek_ring[sa[spi].ekid].value[0]), 
                KEY_SIZE
            );
            if((gcry_error & GPG_ERR_CODE_MASK) != GPG_ERR_NO_ERROR)
            {
                OS_printf(KRED "ERROR: gcry_cipher_setkey error code %d\n" RESET,gcry_error & GPG_ERR_CODE_MASK);
                status = OS_ERROR;
                return status;
            }
            gcry_error = gcry_cipher_setiv(
                tmp_hd, 
                &(sa[tm_frame.tm_sec_header.spi].iv[0]), 
                sa[tm_frame.tm_sec_header.spi].iv_len
            );
            if((gcry_error & GPG_ERR_CODE_MASK) != GPG_ERR_NO_ERROR)
            {
                OS_printf(KRED "ERROR: gcry_cipher_setiv error code %d\n" RESET,gcry_error & GPG_ERR_CODE_MASK);
                status = OS_ERROR;
                return status;
            }
            gcry_error = gcry_cipher_encrypt(
                tmp_hd,
                &(ingest[pdu_loc]),                             // ciphertext output
                pdu_len,			 		                    // length of data
                &(tempTM[pdu_loc]),                             // plaintext input
                pdu_len                                         // in data length
            );
            if((gcry_error & GPG_ERR_CODE_MASK) != GPG_ERR_NO_ERROR)
            {
                OS_printf(KRED "ERROR: gcry_cipher_decrypt error code %d\n" RESET,gcry_error & GPG_ERR_CODE_MASK);
                status = OS_ERROR;
                return status;
            }
            gcry_error = gcry_cipher_authenticate(
                tmp_hd,
                &(aad[0]),                                      // additional authenticated data
                sa[spi].abm_len 		                        // length of AAD
            );
            if((gcry_error & GPG_ERR_CODE_MASK) != GPG_ERR_NO_ERROR)
            {
                OS_printf(KRED "ERROR: gcry_cipher_authenticate error code %d\n" RESET,gcry_error & GPG_ERR_CODE_MASK);
                status = OS_ERROR;
                return status;
            }
            gcry_error = gcry_cipher_gettag(
                tmp_hd,
                &(ingest[mac_loc]),                             // tag output
                MAC_SIZE                                        // tag size
            );
            if((gcry_error & GPG_ERR_CODE_MASK) != GPG_ERR_NO_ERROR)
            {
                OS_printf(KRED "ERROR: gcry_cipher_checktag error code %d\n" RESET,gcry_error & GPG_ERR_CODE_MASK);
                status = OS_ERROR;
                return status;
            }

            #ifdef MAC_DEBUG
                OS_printf("MAC = 0x");
                for(x = 0; x < MAC_SIZE; x++)
                {
                    OS_printf("%02x", (uint8) ingest[x + mac_loc]);
                }
                OS_printf("\n");
            #endif

            // Update OCF
            y = 0;
            for (x = OCF_SIZE; x > 0; x--)
            {
                ingest[fecf_loc - x] = tm_frame.tm_sec_trailer.ocf[y++];
            }

            // Update FECF
            tm_frame.tm_sec_trailer.fecf = Crypto_Calc_FECF((char*) ingest, fecf_loc - 1);
            ingest[fecf_loc] = (uint8) ((tm_frame.tm_sec_trailer.fecf & 0xFF00) >> 8);
            ingest[fecf_loc + 1] = (uint8) (tm_frame.tm_sec_trailer.fecf & 0x00FF); 
        }
        // Authentication
        else if ((sa[spi].est == 0) && 
                 (sa[spi].ast == 1))
        {
            #ifdef DEBUG
                OS_printf(KBLU "Creating a TM - AUTHENTICATED! \n" RESET);
            #endif
            // TODO: Future work. Operationally same as clear.
            CFE_PSP_MemCpy(ingest, tempTM, count);
        }
        // Encryption
        else if ((sa[spi].est == 1) && 
                 (sa[spi].ast == 0))
        {
            #ifdef DEBUG
                OS_printf(KBLU "Creating a TM - ENCRYPTED! \n" RESET);
            #endif
            // TODO: Future work. Operationally same as clear.
            CFE_PSP_MemCpy(ingest, tempTM, count);
        }

    #ifdef TM_DEBUG
        Crypto_tmPrint(&tm_frame);		
    #endif	
    
    #ifdef DEBUG
        OS_printf(KYEL "----- Crypto_TM_ApplySecurity END -----\n" RESET);
    #endif

    *len_ingest = count;
    return status;    
}

int32 Crypto_TM_ProcessSecurity(char* ingest, int* len_ingest)
{
    // Local Variables
    int32 status = OS_SUCCESS;

    #ifdef DEBUG
        OS_printf(KYEL "\n----- Crypto_TM_ProcessSecurity START -----\n" RESET);
    #endif

    // TODO: This whole function!
    len_ingest = len_ingest;
    ingest[0] = ingest[0];

    #ifdef DEBUG
        OS_printf(KYEL "----- Crypto_TM_ProcessSecurity END -----\n" RESET);
    #endif

    return status;
}

int32 Crypto_AOS_ApplySecurity(char* ingest, int* len_ingest)
{
    // Local Variables
    int32 status = OS_SUCCESS;

    #ifdef DEBUG
        OS_printf(KYEL "\n----- Crypto_AOS_ApplySecurity START -----\n" RESET);
    #endif

    // TODO: This whole function!
    len_ingest = len_ingest;
    ingest[0] = ingest[0];

    #ifdef DEBUG
        OS_printf(KYEL "----- Crypto_AOS_ApplySecurity END -----\n" RESET);
    #endif

    return status;
}

int32 Crypto_AOS_ProcessSecurity(char* ingest, int* len_ingest)
{
    // Local Variables
    int32 status = OS_SUCCESS;

    #ifdef DEBUG
        OS_printf(KYEL "\n----- Crypto_AOS_ProcessSecurity START -----\n" RESET);
    #endif

    // TODO: This whole function!
    len_ingest = len_ingest;
    ingest[0] = ingest[0];

    #ifdef DEBUG
        OS_printf(KYEL "----- Crypto_AOS_ProcessSecurity END -----\n" RESET);
    #endif

    return status;
}

int32 Crypto_ApplySecurity(char* ingest, int* len_ingest)
{
    // Local Variables
    int32 status = OS_SUCCESS;

    #ifdef DEBUG
        OS_printf(KYEL "\n----- Crypto_ApplySecurity START -----\n" RESET);
    #endif

    // TODO: This whole function!
    len_ingest = len_ingest;
    ingest[0] = ingest[0];

    #ifdef DEBUG
        OS_printf(KYEL "----- Crypto_ApplySecurity END -----\n" RESET);
    #endif

    return status;
}

int32 Crypto_ProcessSecurity(char* ingest, int* len_ingest)
{
    // Local Variables
    int32 status = OS_SUCCESS;

    #ifdef DEBUG
        OS_printf(KYEL "\n----- Crypto_ProcessSecurity START -----\n" RESET);
    #endif

    // TODO: This whole function!
    len_ingest = len_ingest;
    ingest[0] = ingest[0];

    #ifdef DEBUG
        OS_printf(KYEL "----- Crypto_ProcessSecurity END -----\n" RESET);
    #endif

    return status;
}

#endif