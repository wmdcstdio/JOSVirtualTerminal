/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/memlayout.h>
#include <inc/kbdreg.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/console.h>
#include <kern/trap.h>
#include <kern/picirq.h>
#include <kern/paint.h>

static void cons_intr(int (*proc)(void));
static void cons_putc(int c);

// Stupid I/O delay routine necessitated by historical PC design flaws
static void
delay(void)
{
	inb(0x84);
	inb(0x84);
	inb(0x84);
	inb(0x84);
}

/***** Serial I/O code *****/

#define COM1		0x3F8

#define COM_RX		0	// In:	Receive buffer (DLAB=0)
#define COM_TX		0	// Out: Transmit buffer (DLAB=0)
#define COM_DLL		0	// Out: Divisor Latch Low (DLAB=1)
#define COM_DLM		1	// Out: Divisor Latch High (DLAB=1)
#define COM_IER		1	// Out: Interrupt Enable Register
#define   COM_IER_RDI	0x01	//   Enable receiver data interrupt
#define COM_IIR		2	// In:	Interrupt ID Register
#define COM_FCR		2	// Out: FIFO Control Register
#define COM_LCR		3	// Out: Line Control Register
#define	  COM_LCR_DLAB	0x80	//   Divisor latch access bit
#define	  COM_LCR_WLEN8	0x03	//   Wordlength: 8 bits
#define COM_MCR		4	// Out: Modem Control Register
#define	  COM_MCR_RTS	0x02	// RTS complement
#define	  COM_MCR_DTR	0x01	// DTR complement
#define	  COM_MCR_OUT2	0x08	// Out2 complement
#define COM_LSR		5	// In:	Line Status Register
#define   COM_LSR_DATA	0x01	//   Data available
#define   COM_LSR_TXRDY	0x20	//   Transmit buffer avail
#define   COM_LSR_TSRE	0x40	//   Transmitter off


static int vga256_24bit[256] = { 0x000000, 0x0000a8, 0x00a800, 0x00a8a8, 0xa80000, 0xa800a8, 0xa85400, 0xa8a8a8, 0x545454, 0x5454fc, 0x54fc54, 0x54fcfc, 0xfc5454, 0xfc54fc, 0xfcfc54, 0xfcfcfc, 0x000000, 0x141414, 0x202020, 0x2c2c2c, 0x383838, 0x444444, 0x505050, 0x606060, 0x707070, 0x808080, 0x909090, 0xa0a0a0, 0xb4b4b4, 0xc8c8c8, 0xe0e0e0, 0xfcfcfc, 0x0000fc, 0x4000fc, 0x7c00fc, 0xbc00fc, 0xfc00fc, 0xfc00bc, 0xfc007c, 0xfc0040, 0xfc0000, 0xfc4000, 0xfc7c00, 0xfcbc00, 0xfcfc00, 0xbcfc00, 0x7cfc00, 0x40fc00, 0x00fc00, 0x00fc40, 0x00fc7c, 0x00fcbc, 0x00fcfc, 0x00bcfc, 0x007cfc, 0x0040fc, 0x7c7cfc, 0x9c7cfc, 0xbc7cfc, 0xdc7cfc, 0xfc7cfc, 0xfc7cdc, 0xfc7cbc, 0xfc7c9c, 0xfc7c7c, 0xfc9c7c, 0xfcbc7c, 0xfcdc7c, 0xfcfc7c, 0xdcfc7c, 0xbcfc7c, 0x9cfc7c, 0x7cfc7c, 0x7cfc9c, 0x7cfcbc, 0x7cfcdc, 0x7cfcfc, 0x7cdcfc, 0x7cbcfc, 0x7c9cfc, 0xb4b4fc, 0xc4b4fc, 0xd8b4fc, 0xe8b4fc, 0xfcb4fc, 0xfcb4e8, 0xfcb4d8, 0xfcb4c4, 0xfcb4b4, 0xfcc4b4, 0xfcd8b4, 0xfce8b4, 0xfcfcb4, 0xe8fcb4, 0xd8fcb4, 0xc4fcb4, 0xb4fcb4, 0xb4fcc4, 0xb4fcd8, 0xb4fce8, 0xb4fcfc, 0xb4e8fc, 0xb4d8fc, 0xb4c4fc, 0x000070, 0x1c0070, 0x380070, 0x540070, 0x700070, 0x700054, 0x700038, 0x70001c, 0x700000, 0x701c00, 0x703800, 0x705400, 0x707000, 0x547000, 0x387000, 0x1c7000, 0x007000, 0x00701c, 0x007038, 0x007054, 0x007070, 0x005470, 0x003870, 0x001c70, 0x383870, 0x443870, 0x543870, 0x603870, 0x703870, 0x703860, 0x703854, 0x703844, 0x703838, 0x704438, 0x705438, 0x706038, 0x707038, 0x607038, 0x547038, 0x447038, 0x387038, 0x387044, 0x387054, 0x387060, 0x387070, 0x386070, 0x385470, 0x384470, 0x505070, 0x585070, 0x605070, 0x685070, 0x705070, 0x705068, 0x705060, 0x705058, 0x705050, 0x705850, 0x706050, 0x706850, 0x707050, 0x687050, 0x607050, 0x587050, 0x507050, 0x507058, 0x507060, 0x507068, 0x507070, 0x506870, 0x506070, 0x505870, 0x000040, 0x100040, 0x200040, 0x300040, 0x400040, 0x400030, 0x400020, 0x400010, 0x400000, 0x401000, 0x402000, 0x403000, 0x404000, 0x304000, 0x204000, 0x104000, 0x004000, 0x004010, 0x004020, 0x004030, 0x004040, 0x003040, 0x002040, 0x001040, 0x202040, 0x282040, 0x302040, 0x382040, 0x402040, 0x402038, 0x402030, 0x402028, 0x402020, 0x402820, 0x403020, 0x403820, 0x404020, 0x384020, 0x304020, 0x284020, 0x204020, 0x204028, 0x204030, 0x204038, 0x204040, 0x203840, 0x203040, 0x202840, 0x2c2c40, 0x302c40, 0x342c40, 0x3c2c40, 0x402c40, 0x402c3c, 0x402c34, 0x402c30, 0x402c2c, 0x40302c, 0x40342c, 0x403c2c, 0x40402c, 0x3c402c, 0x34402c, 0x30402c, 0x2c402c, 0x2c4030, 0x2c4034, 0x2c403c, 0x2c4040, 0x2c3c40, 0x2c3440, 0x2c3040, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000 };


void set_palette_index(int index, int r, int g, int b) {
  outb(0x3C8, index);
  outb(0x3C9, r); 
  outb(0x3C9, g); 
  outb(0x3C9, b); 
}

void set_palette_with(int *palette){
    for(int index=0;index<256;index++){
        int value = palette[index];
        set_palette_index(index,
            (value>>18)&0x3f,
            (value>>10)&0x3f,
            (value>>2)&0x3f);
    }  
}

uint8_t read_universal_register(int addr_port, int data_port, uint8_t idx){
    uint8_t tmp = inb(addr_port);
    outb(addr_port, idx);
    uint8_t ret = inb(data_port);
    outb(addr_port, tmp);
    return ret;
}

void set_universal_register(int addr_port, int data_port, uint8_t idx, uint8_t mask, uint8_t data){
    uint8_t tmp = inb(addr_port);
    outb(addr_port, idx);
    uint8_t val = inb(data_port);
    val &= ~mask;
    val |= (data&mask);
    outb(data_port, val);
    outb(addr_port, tmp);
}

void write_universal_register(int addr_port, int data_port, uint8_t idx, uint8_t data){
    uint8_t tmp = inb(addr_port);
    outb(addr_port, idx);
    outb(data_port, data);
    outb(addr_port, tmp);
}

uint8_t read_gc_register(uint8_t idx){
    return read_universal_register(GC_ADDR_PORT,GC_DATA_PORT,idx);
}

uint8_t read_seq_register(uint8_t idx){
    return read_universal_register(SEQ_ADDR_PORT,SEQ_DATA_PORT,idx);
}

uint8_t read_crtc_register(uint8_t idx){
    return read_universal_register(CRTC_ADDR_PORT,CRTC_DATA_PORT,idx);
}

void set_gc_register(uint8_t idx, uint8_t mask, uint8_t data){
    set_universal_register(GC_ADDR_PORT,GC_DATA_PORT,idx,mask,data);
}

void set_seq_register(uint8_t idx, uint8_t mask, uint8_t data){
    set_universal_register(SEQ_ADDR_PORT,SEQ_DATA_PORT,idx,mask,data);
}

void set_crtc_register(uint8_t idx, uint8_t mask, uint8_t data){
    set_universal_register(CRTC_ADDR_PORT,CRTC_DATA_PORT,idx,mask,data);
}

void write_gc_register(uint8_t idx, uint8_t data){
    write_universal_register(GC_ADDR_PORT,GC_DATA_PORT,idx,data);
}

void write_seq_register(uint8_t idx, uint8_t data){
    write_universal_register(SEQ_ADDR_PORT,SEQ_DATA_PORT,idx,data);
}

void write_crtc_register(uint8_t idx, uint8_t data){
    write_universal_register(CRTC_ADDR_PORT,CRTC_DATA_PORT,idx,data);
}

void write_attr_register(uint8_t idx, uint8_t data){
    uint8_t tmp = inb(INPUT_STAT1);//step 1
    uint8_t tmpaddr = inb(ATTR_ADDR_DATA_PORT);//step 2
    outb(ATTR_ADDR_DATA_PORT, idx);//step 3
    //uint8_t tmpdata = inb(ATTR_DATA_READ_PORT);//step 4
    //step 5
    outb(ATTR_ADDR_DATA_PORT,data);//step 6
    outb(ATTR_ADDR_DATA_PORT,tmpaddr);//step 7
}

void set_mode0x13(void){
    // Miscellaneous Output Register
    // odd/even page, horizontal sync, clock 00(320/640 pixel wide), ram enable
    outb(EXT_MISC_WRITE, 0x63);

    // Sequencer registers
    write_seq_register(0x00,0x03); //reset register
    write_seq_register(0x01,0x01); //clocking mode register: 8 dots/char
    write_seq_register(0x02,0x0F); //map mask register, 4 planes enable write
    write_seq_register(0x03,0x00); //character map select register, for text mode
    write_seq_register(0x04,0x0E); //sequencer memory mode register, enable chain 4, disable odd/even host memory, enable extended memory

    // CRT controller registers
    write_crtc_register(0x11,0x0E); //vertical retrace end register: unlock access

    write_crtc_register(0x00,0x5F); //horizontal total register
    write_crtc_register(0x01,0x4F); //end horizontal display register
    write_crtc_register(0x02,0x50); //start horizontal blanking register
    write_crtc_register(0x03,0x82); //end horizontal blanking register
    write_crtc_register(0x04,0x54); //start horizontal retrace register
    write_crtc_register(0x05,0x80); //end horizontal retrace register
    write_crtc_register(0x06,0xBF); //vertical total register
    write_crtc_register(0x07,0x1F); //overflow register
    write_crtc_register(0x08,0x00); //preset row scan register
    write_crtc_register(0x09,0x41); //maximum scan line register
    write_crtc_register(0x10,0x9C); //vertical retrace start register
    //0x11 set above
    write_crtc_register(0x12,0x8F); //vertical display end register
    write_crtc_register(0x13,0x28); //offset register
    write_crtc_register(0x14,0x40); //underline location register: double-word addressing
    write_crtc_register(0x15,0x96); //start vertical blanking register
    write_crtc_register(0x16,0xB9); //end vertical blanking register
    write_crtc_register(0x17,0xA3); //CRTC mode control register: word mode
    write_crtc_register(0x18,0xFF); //line compare register

    //Graphic controller registers
    write_gc_register(0x00,0x00); //set/reset register
    write_gc_register(0x01,0x00); //enable set/reset register: disable
    write_gc_register(0x02,0x00); //color compare register
    write_gc_register(0x03,0x00); //data rotate register: do not operate
    write_gc_register(0x04,0x00); //read map select register
    write_gc_register(0x05,0x40); //graphic mode register: 256-color shift mode, not interleave, not odd/even, read mode 0, write mode 0
    write_gc_register(0x06,0x05); //misc graphic register: memory mapping 0xA0000~0xAFFFF, disable text mode
    write_gc_register(0x07,0x0F); //color don't care register
    write_gc_register(0x08,0xFF); //bit mask register

    // Attribute registers
    write_attr_register(0x00,0x00); //palette register 0
    write_attr_register(0x01,0x01); //palette register 1
    write_attr_register(0x02,0x02); //palette register 2
    write_attr_register(0x03,0x03); //palette register 3
    write_attr_register(0x04,0x04); //palette register 4
    write_attr_register(0x05,0x05); //palette register 5
    write_attr_register(0x06,0x06); //palette register 6
    write_attr_register(0x07,0x07); //palette register 7
    write_attr_register(0x08,0x08); //palette register 8
    write_attr_register(0x09,0x09); //palette register 9
    write_attr_register(0x0A,0x0A); //palette register A
    write_attr_register(0x0B,0x0B); //palette register B
    write_attr_register(0x0C,0x0C); //palette register C
    write_attr_register(0x0D,0x0D); //palette register D
    write_attr_register(0x0E,0x0E); //palette register E
    write_attr_register(0x0F,0x0F); //palette register F
    write_attr_register(0x10,0x41); //attribute mode control register: 8-bit color, graphic mode
    write_attr_register(0x11,0x00); //overscan color register
    write_attr_register(0x12,0x0F); //color plane enable register: all enable
    write_attr_register(0x13,0x00); //horizontal pixel panning register: shift 0
    write_attr_register(0x14,0x00); //color select register


    inb(INPUT_STAT1);
    outb(ATTR_ADDR_DATA_PORT, 0x20);
    set_palette_with(vga256_24bit);

    xy_to_base(1,1);

}



static bool serial_exists;

static int
serial_proc_data(void)
{
	if (!(inb(COM1+COM_LSR) & COM_LSR_DATA))
		return -1;
	return inb(COM1+COM_RX);
}

void
serial_intr(void)
{
	if (serial_exists)
		cons_intr(serial_proc_data);
}

static void
serial_putc(int c)
{
	int i;

	for (i = 0;
	     !(inb(COM1 + COM_LSR) & COM_LSR_TXRDY) && i < 12800;
	     i++)
		delay();

	outb(COM1 + COM_TX, c);
}

static void
serial_init(void)
{
	// Turn off the FIFO
	outb(COM1+COM_FCR, 0);

	// Set speed; requires DLAB latch
	outb(COM1+COM_LCR, COM_LCR_DLAB);
	outb(COM1+COM_DLL, (uint8_t) (115200 / 9600));
	outb(COM1+COM_DLM, 0);

	// 8 data bits, 1 stop bit, parity off; turn off DLAB latch
	outb(COM1+COM_LCR, COM_LCR_WLEN8 & ~COM_LCR_DLAB);

	// No modem controls
	outb(COM1+COM_MCR, 0);
	// Enable rcv interrupts
	outb(COM1+COM_IER, COM_IER_RDI);

	// Clear any preexisting overrun indications and interrupts
	// Serial port doesn't exist if COM_LSR returns 0xFF
	serial_exists = (inb(COM1+COM_LSR) != 0xFF);
	(void) inb(COM1+COM_IIR);
	(void) inb(COM1+COM_RX);

	// Enable serial interrupts
	if (serial_exists)
		irq_setmask_8259A(irq_mask_8259A & ~(1<<IRQ_SERIAL));
}



/***** Parallel port output code *****/
// For information on PC parallel port programming, see the class References
// page.


static void
lpt_putc(int c)
{
	int i;

	for (i = 0; !(inb(0x378+1) & 0x80) && i < 12800; i++)
		delay();
	outb(0x378+0, c);
	outb(0x378+2, 0x08|0x04|0x01);
	outb(0x378+2, 0x08);
}




/***** Text-mode CGA/VGA display output *****/

static unsigned addr_6845;
static uint8_t crt_buf[CHAR_MAX_NUM*2];
static uint16_t crt_pos;

static uint8_t *vga_buf;

static void
cga_init(void)
{
	volatile uint16_t *cp;
	uint16_t was;
	unsigned pos;

	//cp = (uint16_t*) (KERNBASE + CGA_BUF);
    cp = (uint16_t*) (KERNBASE + VGA_GRAPHIC_BUF);
	was = *cp;
	*cp = (uint16_t) 0xA55A;
	if (*cp != 0xA55A) {
		cp = (uint16_t*) (KERNBASE + MONO_BUF);
		addr_6845 = MONO_BASE;
	} else {
		*cp = was;
		addr_6845 = CGA_BASE;
	}

	/* Extract cursor location */
	/*outb(addr_6845, 14);
	pos = inb(addr_6845 + 1) << 8;
	outb(addr_6845, 15);
	pos |= inb(addr_6845 + 1);*/

	crt_pos = 0;

    //crt_buf = NULL;

    //////////////////////////////////////////////////////////////
    vga_buf = (uint8_t*)KERNBASE+0xA0000;
    set_mode0x13();
    for(int i=0;i<320*200;i++) vga_buf[i]=0x0f;
    paint_char(0,0,0,0xff);
    //vgaMode13();
    //set_gc_register(0x6,0x1,0x1);
    //outb(EXT_MISC_WRITE,0x0);

    //set_seq_register(0x2,0xf,0xf);
    //open chain 4 mode
    //set_seq_register(0x4,0x8,0x8);

}


void paint_crt_buf(void){
        paint_rect(0,0,VGA_WIDTH,VGA_HEIGHT,0x0f);
    for(int i=0;i<CHAR_MAX_ROW;i++){
        for(int j=0;j<CHAR_MAX_COL;j++){
            int cid=i*CHAR_MAX_COL+j;
            int x=i*CHAR_HEIGHT,y=j*CHAR_WIDTH;
            paint_char(x,y,crt_buf[cid],0xff);
        }
    }
}

static void
cga_putc(int ci)
{
        uint8_t c=(uint8_t)ci;
	// if no attribute given, then use black on white
	switch (c) {
	case '\b':
		if (crt_pos > 0) {
			crt_pos--;
			crt_buf[crt_pos] = ' ';
		}
		break;
	case '\n':
		crt_pos += CHAR_MAX_COL;
		/* fallthru */
	case '\r':
		crt_pos -= (crt_pos % CHAR_MAX_COL);
		break;
	case '\t':
		cons_putc(' ');
		cons_putc(' ');
		cons_putc(' ');
		cons_putc(' ');
		cons_putc(' ');
		break;
	default:
		crt_buf[crt_pos++] = c;		/* write the character */
		break;
	}

	// What is the purpose of this?
	if (crt_pos >= CHAR_MAX_NUM) {
		int i;

		memmove(crt_buf, crt_buf + CHAR_MAX_COL, (CHAR_MAX_NUM - CHAR_MAX_COL) * sizeof(uint8_t));
		for (i = CHAR_MAX_NUM - CHAR_MAX_COL; i < CHAR_MAX_NUM; i++)
			crt_buf[i] = ' ';
		crt_pos -= CHAR_MAX_COL;
	}

    paint_crt_buf();

	/* move that little blinky thing */
	/*outb(addr_6845, 14);
	outb(addr_6845 + 1, crt_pos >> 8);
	outb(addr_6845, 15);
	outb(addr_6845 + 1, crt_pos);*/
}


/***** Keyboard input code *****/

#define NO		0

#define SHIFT		(1<<0)
#define CTL		(1<<1)
#define ALT		(1<<2)

#define CAPSLOCK	(1<<3)
#define NUMLOCK		(1<<4)
#define SCROLLLOCK	(1<<5)

#define E0ESC		(1<<6)

static uint8_t shiftcode[256] =
{
	[0x1D] = CTL,
	[0x2A] = SHIFT,
	[0x36] = SHIFT,
	[0x38] = ALT,
	[0x9D] = CTL,
	[0xB8] = ALT
};

static uint8_t togglecode[256] =
{
	[0x3A] = CAPSLOCK,
	[0x45] = NUMLOCK,
	[0x46] = SCROLLLOCK
};

static uint8_t normalmap[256] =
{
	NO,   0x1B, '1',  '2',  '3',  '4',  '5',  '6',	// 0x00
	'7',  '8',  '9',  '0',  '-',  '=',  '\b', '\t',
	'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',	// 0x10
	'o',  'p',  '[',  ']',  '\n', NO,   'a',  's',
	'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',	// 0x20
	'\'', '`',  NO,   '\\', 'z',  'x',  'c',  'v',
	'b',  'n',  'm',  ',',  '.',  '/',  NO,   '*',	// 0x30
	NO,   ' ',  NO,   NO,   NO,   NO,   NO,   NO,
	NO,   NO,   NO,   NO,   NO,   NO,   NO,   '7',	// 0x40
	'8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
	'2',  '3',  '0',  '.',  NO,   NO,   NO,   NO,	// 0x50
	[0xC7] = KEY_HOME,	      [0x9C] = '\n' /*KP_Enter*/,
	[0xB5] = '/' /*KP_Div*/,      [0xC8] = KEY_UP,
	[0xC9] = KEY_PGUP,	      [0xCB] = KEY_LF,
	[0xCD] = KEY_RT,	      [0xCF] = KEY_END,
	[0xD0] = KEY_DN,	      [0xD1] = KEY_PGDN,
	[0xD2] = KEY_INS,	      [0xD3] = KEY_DEL
};

static uint8_t shiftmap[256] =
{
	NO,   033,  '!',  '@',  '#',  '$',  '%',  '^',	// 0x00
	'&',  '*',  '(',  ')',  '_',  '+',  '\b', '\t',
	'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',	// 0x10
	'O',  'P',  '{',  '}',  '\n', NO,   'A',  'S',
	'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',	// 0x20
	'"',  '~',  NO,   '|',  'Z',  'X',  'C',  'V',
	'B',  'N',  'M',  '<',  '>',  '?',  NO,   '*',	// 0x30
	NO,   ' ',  NO,   NO,   NO,   NO,   NO,   NO,
	NO,   NO,   NO,   NO,   NO,   NO,   NO,   '7',	// 0x40
	'8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
	'2',  '3',  '0',  '.',  NO,   NO,   NO,   NO,	// 0x50
	[0xC7] = KEY_HOME,	      [0x9C] = '\n' /*KP_Enter*/,
	[0xB5] = '/' /*KP_Div*/,      [0xC8] = KEY_UP,
	[0xC9] = KEY_PGUP,	      [0xCB] = KEY_LF,
	[0xCD] = KEY_RT,	      [0xCF] = KEY_END,
	[0xD0] = KEY_DN,	      [0xD1] = KEY_PGDN,
	[0xD2] = KEY_INS,	      [0xD3] = KEY_DEL
};

#define C(x) (x - '@')

static uint8_t ctlmap[256] =
{
	NO,      NO,      NO,      NO,      NO,      NO,      NO,      NO,
	NO,      NO,      NO,      NO,      NO,      NO,      NO,      NO,
	C('Q'),  C('W'),  C('E'),  C('R'),  C('T'),  C('Y'),  C('U'),  C('I'),
	C('O'),  C('P'),  NO,      NO,      '\r',    NO,      C('A'),  C('S'),
	C('D'),  C('F'),  C('G'),  C('H'),  C('J'),  C('K'),  C('L'),  NO,
	NO,      NO,      NO,      C('\\'), C('Z'),  C('X'),  C('C'),  C('V'),
	C('B'),  C('N'),  C('M'),  NO,      NO,      C('/'),  NO,      NO,
	[0x97] = KEY_HOME,
	[0xB5] = C('/'),		[0xC8] = KEY_UP,
	[0xC9] = KEY_PGUP,		[0xCB] = KEY_LF,
	[0xCD] = KEY_RT,		[0xCF] = KEY_END,
	[0xD0] = KEY_DN,		[0xD1] = KEY_PGDN,
	[0xD2] = KEY_INS,		[0xD3] = KEY_DEL
};

static uint8_t *charcode[4] = {
	normalmap,
	shiftmap,
	ctlmap,
	ctlmap
};

/*
 * Get data from the keyboard.  If we finish a character, return it.  Else 0.
 * Return -1 if no data.
 */
int flg=0;
static int
kbd_proc_data(void)
{
        //paint_char(0,0,'A',0xff);
    /*for (int i = 0; i < VGA_HEIGHT; ++i) {
	for (int j = 0; j < VGA_WIDTH; ++j) {
		paint_point(i, j, 0xc0);
	}
    }*/
/*
    uint8_t *low=(uint8_t*)KERNBASE+0xa0000;
    for(uint8_t *i=low;i<low+320*200;i++){
        if(i<low+320*100) *i=0xc0*flg;
        else{
            *i=0xf0;
        }
<<<<<<< HEAD
    }*/
    //return 0;
	int c;
	uint8_t stat, data;
	static uint32_t shift;

	stat = inb(KBSTATP);
	if ((stat & KBS_DIB) == 0)
		return -1;
	// Ignore data from mouse.
	if (stat & KBS_TERR)
		return -1;

	data = inb(KBDATAP);

	if (data == 0xE0) {
		// E0 escape character
		shift |= E0ESC;
		return 0;
	} else if (data & 0x80) {
		// Key released
		data = (shift & E0ESC ? data : data & 0x7F);
		shift &= ~(shiftcode[data] | E0ESC);
		return 0;
	} else if (shift & E0ESC) {
		// Last character was an E0 escape; or with 0x80
		data |= 0x80;
		shift &= ~E0ESC;
	}

	shift |= shiftcode[data];
	shift ^= togglecode[data];

	c = charcode[shift & (CTL | SHIFT)][data];
	if (shift & CAPSLOCK) {
		if ('a' <= c && c <= 'z')
			c += 'A' - 'a';
		else if ('A' <= c && c <= 'Z')
			c += 'a' - 'A';
	}

	// Process special keys
	// Ctrl-Alt-Del: reboot
	if (!(~shift & (CTL | ALT)) && c == KEY_DEL) {
		cprintf("Rebooting!\n");
		outb(0x92, 0x3); // courtesy of Chris Frost
	}

	return c;
}

void
kbd_intr(void)
{
	cons_intr(kbd_proc_data);
}

static void
kbd_init(void)
{
	// Drain the kbd buffer so that QEMU generates interrupts.
	kbd_intr();
	irq_setmask_8259A(irq_mask_8259A & ~(1<<IRQ_KBD));
}



/***** General device-independent console code *****/
// Here we manage the console input buffer,
// where we stash characters received from the keyboard or serial port
// whenever the corresponding interrupt occurs.

#define CONSBUFSIZE 512

static struct {
	uint8_t buf[CONSBUFSIZE];
	uint32_t rpos;
	uint32_t wpos;
} cons;

// called by device interrupt routines to feed input characters
// into the circular console input buffer.
static void
cons_intr(int (*proc)(void))
{
	int c;

	while ((c = (*proc)()) != -1) {
		if (c == 0)
			continue;
		cons.buf[cons.wpos++] = c;
		if (cons.wpos == CONSBUFSIZE)
			cons.wpos = 0;
	}
}

// return the next input character from the console, or 0 if none waiting
int
cons_getc(void)
{
	int c;

	// poll for any pending input characters,
	// so that this function works even when interrupts are disabled
	// (e.g., when called from the kernel monitor).
	serial_intr();
	kbd_intr();

	// grab the next character from the input buffer.
	if (cons.rpos != cons.wpos) {
		c = cons.buf[cons.rpos++];
		if (cons.rpos == CONSBUFSIZE)
			cons.rpos = 0;
		return c;
	}
	return 0;
}

// output a character to the console
static void
cons_putc(int c)
{
	serial_putc(c);
	lpt_putc(c);
	cga_putc(c);
}

// initialize the console devices
void
cons_init(void)
{
	cga_init();
	kbd_init();
	serial_init();

	if (!serial_exists)
		cprintf("Serial port does not exist!\n");
}


// `High'-level console I/O.  Used by readline and cprintf.

void
cputchar(int c)
{
	cons_putc(c);
}

int
getchar(void)
{
	int c;

	while ((c = cons_getc()) == 0)
		/* do nothing */;
	return c;
}

int
iscons(int fdnum)
{
	// used by readline
	return 1;
}
