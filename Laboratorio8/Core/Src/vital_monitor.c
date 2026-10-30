#include "vital_monitor.h"
#include <stdint.h>
#include <string.h>

/* ILI9341, 240 x 320, vertical. Editar estos pines si es necesario. */
#define TFT_CS_PORT   LCD_CS_GPIO_Port
#define TFT_CS_PIN    LCD_CS_Pin
#define TFT_DC_PORT   LCD_DC_GPIO_Port
#define TFT_DC_PIN    LCD_DC_Pin
#define TFT_RST_PORT  LCD_RESET_GPIO_Port
#define TFT_RST_PIN   LCD_RESET_Pin
#define W 240
#define H 320
#define RGB565(r,g,b) (uint16_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|((b)>>3))
#define BG      RGB565(6,17,30)
#define PANEL   RGB565(16,37,55)
#define WHITE   RGB565(240,246,252)
#define MUTED   RGB565(149,170,187)
#define RED     RGB565(255,100,120)
#define CYAN    RGB565(52,203,246)
#define GREEN   RGB565(67,222,144)
#define YELLOW  RGB565(255,199,76)
#define GRID    RGB565(30,65,68)

static SPI_HandleTypeDef *tft_spi;
static void cs(uint8_t high){HAL_GPIO_WritePin(TFT_CS_PORT,TFT_CS_PIN,high?GPIO_PIN_SET:GPIO_PIN_RESET);}
static void dc(uint8_t high){HAL_GPIO_WritePin(TFT_DC_PORT,TFT_DC_PIN,high?GPIO_PIN_SET:GPIO_PIN_RESET);}
static void tx(const uint8_t *p,uint16_t n){HAL_SPI_Transmit(tft_spi,(uint8_t*)p,n,HAL_MAX_DELAY);}
static void cmd(uint8_t c){dc(0);cs(0);tx(&c,1);cs(1);}
static void data(const uint8_t *p,uint16_t n){dc(1);cs(0);tx(p,n);cs(1);}
static void reg(uint8_t c,const uint8_t *p,uint16_t n){cmd(c);if(n)data(p,n);}
static void reset(void){HAL_GPIO_WritePin(TFT_RST_PORT,TFT_RST_PIN,GPIO_PIN_RESET);HAL_Delay(20);HAL_GPIO_WritePin(TFT_RST_PORT,TFT_RST_PIN,GPIO_PIN_SET);HAL_Delay(150);}
static void window(uint16_t x0,uint16_t y0,uint16_t x1,uint16_t y1){
    uint8_t a[4];
    a[0]=x0>>8;a[1]=x0;a[2]=x1>>8;a[3]=x1;reg(0x2A,a,4);
    a[0]=y0>>8;a[1]=y0;a[2]=y1>>8;a[3]=y1;reg(0x2B,a,4);
    cmd(0x2C);
}
static void fill(int x,int y,int w,int h,uint16_t color){
    if(x<0){w+=x;x=0;}if(y<0){h+=y;y=0;}
    if(x>=W||y>=H||w<=0||h<=0)return;
    if(x+w>W)w=W-x;if(y+h>H)h=H-y;
    window(x,y,x+w-1,y+h-1);
    uint8_t block[128];for(int i=0;i<64;i++){block[2*i]=color>>8;block[2*i+1]=color;}
    uint32_t left=(uint32_t)w*h;
    dc(1);cs(0);
    while(left){uint16_t pixels=(left>64)?64:(uint16_t)left;tx(block,2*pixels);left-=pixels;}
    cs(1);
}
static void pixel(int x,int y,uint16_t color){fill(x,y,1,1,color);}
static void line(int x0,int y0,int x1,int y1,uint16_t color){
    int dx=(x1>x0)?x1-x0:x0-x1,sx=x0<x1?1:-1;
    int dy=-((y1>y0)?y1-y0:y0-y1),sy=y0<y1?1:-1,err=dx+dy;
    while(1){pixel(x0,y0,color);if(x0==x1&&y0==y1)break;
        int e2=2*err;if(e2>=dy){err+=dy;x0+=sx;}if(e2<=dx){err+=dx;y0+=sy;}}
}
static void border(int x,int y,int w,int h,uint16_t color){fill(x,y,w,1,color);fill(x,y+h-1,w,1,color);fill(x,y,1,h,color);fill(x+w-1,y,1,h,color);}

/* Fuente 5x7 almacenada como mapa de columnas. Letras mayusculas. */
static const char alphabet[]=" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789%:.-/";
static const uint8_t font[][5]={
{0,0,0,0,0}, /* espacio */
{0x7E,0x11,0x11,0x11,0x7E}, /* A */
{0x7F,0x49,0x49,0x49,0x36}, /* B */
{0x3E,0x41,0x41,0x41,0x22}, /* C */
{0x7F,0x41,0x41,0x22,0x1C}, /* D */
{0x7F,0x49,0x49,0x49,0x41}, /* E */
{0x7F,0x09,0x09,0x09,0x01}, /* F */
{0x3E,0x41,0x49,0x49,0x7A}, /* G */
{0x7F,0x08,0x08,0x08,0x7F}, /* H */
{0x00,0x41,0x7F,0x41,0x00}, /* I */
{0x20,0x40,0x41,0x3F,0x01}, /* J */
{0x7F,0x08,0x14,0x22,0x41}, /* K */
{0x7F,0x40,0x40,0x40,0x40}, /* L */
{0x7F,0x02,0x0C,0x02,0x7F}, /* M */
{0x7F,0x04,0x08,0x10,0x7F}, /* N */
{0x3E,0x41,0x41,0x41,0x3E}, /* O */
{0x7F,0x09,0x09,0x09,0x06}, /* P */
{0x3E,0x41,0x51,0x21,0x5E}, /* Q */
{0x7F,0x09,0x19,0x29,0x46}, /* R */
{0x46,0x49,0x49,0x49,0x31}, /* S */
{0x01,0x01,0x7F,0x01,0x01}, /* T */
{0x3F,0x40,0x40,0x40,0x3F}, /* U */
{0x1F,0x20,0x40,0x20,0x1F}, /* V */
{0x7F,0x20,0x18,0x20,0x7F}, /* W */
{0x63,0x14,0x08,0x14,0x63}, /* X */
{0x03,0x04,0x78,0x04,0x03}, /* Y */
{0x61,0x51,0x49,0x45,0x43}, /* Z */
{0x3E,0x51,0x49,0x45,0x3E}, /* 0 */
{0x00,0x42,0x7F,0x40,0x00}, /* 1 */
{0x42,0x61,0x51,0x49,0x46}, /* 2 */
{0x21,0x41,0x45,0x4B,0x31}, /* 3 */
{0x18,0x14,0x12,0x7F,0x10}, /* 4 */
{0x27,0x45,0x45,0x45,0x39}, /* 5 */
{0x3C,0x4A,0x49,0x49,0x30}, /* 6 */
{0x01,0x71,0x09,0x05,0x03}, /* 7 */
{0x36,0x49,0x49,0x49,0x36}, /* 8 */
{0x06,0x49,0x49,0x29,0x1E}, /* 9 */
{0x63,0x13,0x08,0x64,0x63}, /* % */
{0x00,0x36,0x36,0x00,0x00}, /* : */
{0x08,0x08,0x08,0x08,0x08}, /* - */
{0x00,0x60,0x60,0x00,0x00}, /* . */
{0x20,0x10,0x08,0x04,0x02}  /* / */
};
static void letter(int x,int y,char ch,int scale,uint16_t color){
    const char *p=strchr(alphabet,ch);if(!p)return;
    const uint8_t *g=font[p-alphabet];
    for(int c=0;c<5;c++)for(int r=0;r<7;r++)if(g[c]&(1<<r))fill(x+c*scale,y+r*scale,scale,scale,color);
}
static void print(int x,int y,const char *s,int scale,uint16_t color){
    while(*s){letter(x,y,*s,scale,color);x+=6*scale;s++;}
}
/* Imagen bitmap de un corazon: 16x16, 1 bit por pixel, 16 filas. */
static const uint16_t heart[16]={
0x0000,0x1C38,0x3E7C,0x7FFE,0xFFFF,0xFFFF,0xFFFF,0x7FFE,
0x3FFC,0x1FF8,0x0FF0,0x07E0,0x03C0,0x0180,0x0000,0x0000
};
static void heart_icon(int x,int y,int scale,uint16_t color){
    for(int row=0;row<16;row++)for(int col=0;col<16;col++)
        if(heart[row]&(0x8000u>>col))fill(x+col*scale,y+row*scale,scale,scale,color);
}
void VitalMonitor_Init(SPI_HandleTypeDef *spi){
    tft_spi=spi;
    cs(1);dc(1);reset();
    cmd(0x01);HAL_Delay(150); // Software reset
    {uint8_t v=0x48;reg(0x36,&v,1);} // MADCTL vertical, BGR
    {uint8_t v=0x55;reg(0x3A,&v,1);} // RGB565 / 16 bits
    {uint8_t v=0x00;reg(0x20,&v,0);} // Inversion off
    cmd(0x11);HAL_Delay(150); // Sleep out
    cmd(0x29);HAL_Delay(50);  // Display on
}
void VitalMonitor_Draw(void){
    fill(0,0,W,H,BG);
    fill(0,0,W,39,PANEL);heart_icon(10,5,2,RED);
    print(48,13,"VITAL MONITOR",2,WHITE);
    print(10,47,"SIGNOS VITALES",2,MUTED);

    fill(8,70,108,77,PANEL);border(8,70,108,77,RED);
    print(15,77,"PULSO",2,MUTED);
    print(19,101,"76",4,RED);
    print(78,125,"BPM",1,WHITE);

    fill(124,70,108,77,PANEL);border(124,70,108,77,CYAN);
    print(133,77,"SPO2",2,MUTED);
    print(135,101,"98",4,CYAN);
    print(198,125,"%",2,WHITE);

    fill(8,156,224,43,PANEL);
    print(16,165,"TEMP",2,MUTED);
    print(110,165,"36.8",2,YELLOW);
    print(183,165,"C",2,WHITE);

    fill(8,208,224,81,PANEL);
    print(15,214,"ECG - SENAL DEMO",1,GREEN);
    for(int x=16;x<226;x+=20)line(x,229,x,281,GRID);
    for(int y=235;y<281;y+=12)line(16,y,224,y,GRID);
    const int ecg[][2]={{17,259},{36,259},{44,256},{51,259},{60,259},{67,270},{75,232},{83,278},{91,259},{108,259},{121,253},{132,259},{153,259},{161,256},{168,259},{177,259},{184,270},{192,232},{200,278},{208,259},{224,259}};
    for(unsigned i=1;i<sizeof(ecg)/sizeof(ecg[0]);i++)line(ecg[i-1][0],ecg[i-1][1],ecg[i][0],ecg[i][1],GREEN);
    print(20,298,"MONITOREO SIMULADO",1,MUTED);
}
