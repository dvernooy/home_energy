/*********************************************
 * vim:sw=8:ts=8:si:et
 * To use the above modeline in vim you must have "set modeline" in your .vimrc
 * Author: Guido Socher
 * Copyright: GPL V2
 *
 * A very basic web server. 
 *
 * http://tuxgraphics.org/electronics/
 * Chip type           : Atmega88/168/328/644/1284P with ENC28J60
 *********************************************/
 
/****************************
changes to go to standalone
1. Pull the Vtarget jumper
2. 12 MHz FCPU ... enc28j60.c, system.h, timeout.h (10 MHz for board)
3. set lcd contrast 0x36 (0xc8 for board)
4. IP ADDRESS: 192.168.3.2 vs. 192.168.2.16
*****************************/

/**********
V2:
1. fixed counter
2. Multiple web pages ... rescaled page & numbers-only page
3. added number only page
4. Fixed current sensor issue
**************/
/**********
V3:
1. fixed PF2 error
2. More output to data page
3. removed 2s reload on data page
4. Slimmed javascript to not overload Ethernet when 3 digits sent
5. Added 2 hr time series (1x every 48s)
6. Ethernet packet breakdown
	a. 1500 bytes
	b. -28 overhead for TCP/IP = 1472 bytes
	c. -84 for pkt header = 1388 bytes
	d. -1368 for basic js file (+/- 5 depending on context) = 20 +/- 5 bytes
	e. *** 20 +/- 5 bytes buffer *****
**************/

/**********
V4:
1. added gas monitoring to PB0 
2. added gas counter & inter-count timing to /c webpage
**************/

/**********
V10:
1. moved SEI. Added done flag
2. changed clock to 12MHz to match crystal
3. Note, loop time "temp" is 67ms ...need to reconcile
**************/

/**********
V11:
1. rewrote ISR with delay
2. added forced check in main
**************/


/************
To do still:
1. average outputs
************/
 
#include <avr/io.h>
#include <stdbool.h>
#include <avr/interrupt.h>
#include <inttypes.h>
#include <util/delay.h>
#include <math.h>
#include <avr/pgmspace.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ip_arp_udp_tcp.h"
#include "enc28j60.h"
#include "timeout.h"
#include "lcd.h"
#include "time.h"

// set output to VCC, red LED off
#define LEDOFF PORTB|=(1<<PORTB1)
// set output to GND, red LED on
#define LEDON PORTB&=~(1<<PORTB1)
// to test the state of the LED
#define LEDISOFF PORTB&(1<<PORTB1)

#define	N_POINTS 139

#define	N_WEB  150
#define N_DISP 150
#define N_AVG 100


/********************************************************************************
Global Variables
********************************************************************************/
static FILE lcd_out = FDEV_SETUP_STREAM(lcd_chr_printf, NULL, _FDEV_SETUP_WRITE);

// please modify the following two lines. mac and ip have to be unique
// in your local area network. You can not have the same numbers in
// two devices:
// how did I get the mac addr? Translate the first 3 numbers into ascii is: TUX
static uint8_t mymac[6] = {0x54,0x55,0x58,0x10,0x00,0x29};
static uint8_t myip[4] = {192,168,2,16}; // aka http://10.0.0.29/
//static uint8_t myip[4] = {192,168,3,2}; // aka http://10.0.0.29/

//static uint8_t myip[4] = {192,168,2,17}; // aka http://10.0.0.29/


// server listen port for www
#define MYWWWPORT 80

// global packet buffer
#define BUFFER_SIZE 1475
static uint8_t buf[BUFFER_SIZE+1];

// global string buffer
#define STR_BUFFER_SIZE 20
static char gStrbuf[STR_BUFFER_SIZE+1];

volatile uint16_t buffer0[N_POINTS];			
volatile uint16_t buffer1[N_POINTS];		
volatile uint16_t buffer2[N_POINTS];			
uint16_t power_vector_disp[N_DISP];		
uint16_t power_vector_disp2h[N_DISP];		
double v[N_POINTS];			
double i1[N_POINTS];		
double i2[N_POINTS];		
double power_vector[N_WEB];
double power_vector2h[N_WEB];

//double power_vector_old[N_WEB];
double P1, VAR1, VA1, PF1, P2, VAR2, VA2, PF2;
double P1_vec[N_AVG], VAR1_vec[N_AVG], VA1_vec[N_AVG], PF1_vec[N_AVG], P2_vec[N_AVG], VAR2_vec[N_AVG], VA2_vec[N_AVG], PF2_vec[N_AVG];
double vrms, i1rms, i2rms, v_i1, v_i2, max_tracking1, temp_power;
volatile uint8_t done, channel;
volatile uint16_t count;
uint16_t j, max_tracking, temp_power_disp, overall_counter5m, overall_counter2h, P1_disp, P2_disp, PF1_disp, PF2_disp;


//UINT32 temp, temp1, current_time, last_time, gas_delay_time;
uint32_t temp, temp1, current_time, last_time, gas_delay_time, gas_count;
TIME t;

uint16_t pulse_high, counter, STATE_IS_LOW_WAITING_FOR_HIGH = 1, MAX_COUNTER;

uint16_t http200ok(void)
{
        return(fill_tcp_data_p(buf,0,PSTR("HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nPragma: no-cache\r\n\r\n")));
}

uint16_t fill_tcp_data_int(uint8_t *buf,uint16_t plen,int16_t i)
{
        itoa(i,gStrbuf,10); // convert integer to string
        return(fill_tcp_data(buf,plen,gStrbuf));
}


// prepare the webpage by writing the data to the tcp send buffer
uint16_t print_webpage5m(uint8_t *buf, uint16_t *data, uint16_t power)
{       cli(); 
        uint16_t plen;
		uint16_t j;
	  //plen=http200ok();
		plen=fill_tcp_data_p(buf,0,PSTR("HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nPragma: no-cache\r\nRefresh: 2\r\n\r\n"));
	  //uint8_t mydata[10] = {1,4,1,1,6,1,2,1,1,1};
	  //plen=fill_tcp_data_p(buf,plen,PSTR("<pre><script>var p=[1,4,1,1,6,1,2,1,1,1];</script><body onload=\"c(p)\">"));
		plen=fill_tcp_data_p(buf,plen,PSTR("<a href=/r>[4X]</a>"));
		plen=fill_tcp_data_p(buf,plen,PSTR("<a href=/s>[2h]</a>"));
		plen=fill_tcp_data_p(buf,plen,PSTR("<body onload=\"c(p)\"><canvas id=\"g\"width=\"501\"height=\"501\">"));
		plen=fill_tcp_data_p(buf,plen,PSTR("</canvas><script type=\"text/javascript\">var p=["));
		for (j = 0; j< (N_DISP-1); j++){
		_delay_ms(1);
		plen=fill_tcp_data_int(buf,plen,data[j]/10);
		plen=fill_tcp_data_p(buf,plen,PSTR(","));
		}
		plen=fill_tcp_data_int(buf,plen,data[N_DISP-1]/10);
        plen=fill_tcp_data_p(buf,plen,PSTR("];function c(p){var r=[];for(x=-240,i=0;i<=150;i++)"));
        plen=fill_tcp_data_p(buf,plen,PSTR("{y=p[i];r.push([x,y]);x+=3;}d(r);}function d(r)"));
		plen=fill_tcp_data_p(buf,plen,PSTR("{var w=document.getElementById('g');var i,x,y;"));
		plen=fill_tcp_data_p(buf,plen,PSTR("if(w.getContext){var s=0.5;var z=w.getContext('2d');"));
//		plen=fill_tcp_data_p(buf,plen,PSTR("z.strokeStyle=\"#000000\";z.lineWidth=1;z.beginPath();"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.moveTo(15,15);z.lineTo(15,475);"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.moveTo(15,475);z.lineTo(475,475);"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.stroke();z.save();z.translate(250,475);"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.scale(1.0,-1.0);z.strokeStyle=\"#FF0000\";"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.beginPath();if(r.length>0)"));
		plen=fill_tcp_data_p(buf,plen,PSTR("{x=r[0][0]*2*s;y=r[0][1]*s;z.moveTo(x,y);"));
		plen=fill_tcp_data_p(buf,plen,PSTR("for(i=0;i<r.length;i+=1)"));
		plen=fill_tcp_data_p(buf,plen,PSTR("{x=r[i][0]*2*s;y=r[i][1]*s;z.lineTo(x,y);}"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.stroke();}z.restore();"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.font='20px sans-serif';z.textBaseline='top';"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.fillText('"));
		plen=fill_tcp_data_int(buf,plen,power);
		plen=fill_tcp_data_p(buf,plen,PSTR(" W',200,0);z.fillText('5min',200,480);"));
		plen=fill_tcp_data_p(buf,plen,PSTR("}}</script></body>"));
        return(plen);
}


// prepare the webpage by writing the data to the tcp send buffer
uint16_t print_webpage_reduced5m(uint8_t *buf, uint16_t *data, uint16_t power)
{       cli(); 
        uint16_t plen;
		uint16_t j;
	  //plen=http200ok();
		plen=fill_tcp_data_p(buf,0,PSTR("HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nPragma: no-cache\r\nRefresh: 2\r\n\r\n"));
	  //uint8_t mydata[10] = {1,4,1,1,6,1,2,1,1,1};
	  //plen=fill_tcp_data_p(buf,plen,PSTR("<pre><script>var p=[1,4,1,1,6,1,2,1,1,1];</script><body onload=\"c(p)\">"));
		plen=fill_tcp_data_p(buf,plen,PSTR("<a href=/>[1X]</a>"));
		plen=fill_tcp_data_p(buf,plen,PSTR("<a href=/t>[2h]</a>"));
		plen=fill_tcp_data_p(buf,plen,PSTR("<body onload=\"c(p)\"><canvas id=\"g\"width=\"501\"height=\"501\">"));
		plen=fill_tcp_data_p(buf,plen,PSTR("</canvas><script type=\"text/javascript\">var p=["));
		for (j = 0; j< (N_DISP-1); j++){
		_delay_ms(1);
		plen=fill_tcp_data_int(buf,plen,data[j]/10);
		plen=fill_tcp_data_p(buf,plen,PSTR(","));
		}
		plen=fill_tcp_data_int(buf,plen,data[N_DISP-1]/10);
        plen=fill_tcp_data_p(buf,plen,PSTR("];function c(p){var r=[];for(x=-240,i=0;i<=150;i++)"));
        plen=fill_tcp_data_p(buf,plen,PSTR("{y=p[i];r.push([x,y]);x+=3;}d(r);}function d(r)"));
		plen=fill_tcp_data_p(buf,plen,PSTR("{var w=document.getElementById('g');var i,x,y;"));
		plen=fill_tcp_data_p(buf,plen,PSTR("if(w.getContext){var s=2;var z=w.getContext('2d');"));
//		plen=fill_tcp_data_p(buf,plen,PSTR("z.strokeStyle=\"#000000\";z.lineWidth=1;z.beginPath();"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.moveTo(15,15);z.lineTo(15,475);"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.moveTo(15,475);z.lineTo(475,475);"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.stroke();z.save();z.translate(250,475);"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.scale(1.0,-1.0);z.strokeStyle=\"#FF0000\";"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.beginPath();if(r.length>0)"));
		plen=fill_tcp_data_p(buf,plen,PSTR("{x=r[0][0]*0.5*s;y=r[0][1]*s;z.moveTo(x,y);"));
		plen=fill_tcp_data_p(buf,plen,PSTR("for(i=0;i<r.length;i+=1)"));
		plen=fill_tcp_data_p(buf,plen,PSTR("{x=r[i][0]*0.5*s;y=r[i][1]*s;z.lineTo(x,y);}"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.stroke();}z.restore();"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.font='20px sans-serif';z.textBaseline='top';"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.fillText('"));
		plen=fill_tcp_data_int(buf,plen,power);
		plen=fill_tcp_data_p(buf,plen,PSTR(" W',200,0);z.fillText('5min',200,480);"));
		plen=fill_tcp_data_p(buf,plen,PSTR("}}</script></body>"));
        return(plen);
}


// prepare the webpage by writing the data to the tcp send buffer
uint16_t print_webpage2h(uint8_t *buf, uint16_t *data, uint16_t power)
{       cli(); 
        uint16_t plen;
		uint16_t j;
	  //plen=http200ok();
		plen=fill_tcp_data_p(buf,0,PSTR("HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nPragma: no-cache\r\nRefresh: 2\r\n\r\n"));
	  //uint8_t mydata[10] = {1,4,1,1,6,1,2,1,1,1};
	  //plen=fill_tcp_data_p(buf,plen,PSTR("<pre><script>var p=[1,4,1,1,6,1,2,1,1,1];</script><body onload=\"c(p)\">"));
		plen=fill_tcp_data_p(buf,plen,PSTR("<a href=/t>[4X]</a>"));
		plen=fill_tcp_data_p(buf,plen,PSTR("<a href=/>[5m]</a>"));
		plen=fill_tcp_data_p(buf,plen,PSTR("<body onload=\"c(p)\"><canvas id=\"g\"width=\"501\"height=\"501\">"));
		plen=fill_tcp_data_p(buf,plen,PSTR("</canvas><script type=\"text/javascript\">var p=["));
		for (j = 0; j< (N_DISP-1); j++){
		_delay_ms(1);
		plen=fill_tcp_data_int(buf,plen,data[j]/10);
		plen=fill_tcp_data_p(buf,plen,PSTR(","));
		}
		plen=fill_tcp_data_int(buf,plen,data[N_DISP-1]/10);
        plen=fill_tcp_data_p(buf,plen,PSTR("];function c(p){var r=[];for(x=-240,i=0;i<=150;i++)"));
        plen=fill_tcp_data_p(buf,plen,PSTR("{y=p[i];r.push([x,y]);x+=3;}d(r);}function d(r)"));
		plen=fill_tcp_data_p(buf,plen,PSTR("{var w=document.getElementById('g');var i,x,y;"));
		plen=fill_tcp_data_p(buf,plen,PSTR("if(w.getContext){var s=0.5;var z=w.getContext('2d');"));
//		plen=fill_tcp_data_p(buf,plen,PSTR("z.strokeStyle=\"#000000\";z.lineWidth=1;z.beginPath();"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.moveTo(15,15);z.lineTo(15,475);"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.moveTo(15,475);z.lineTo(475,475);"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.stroke();z.save();z.translate(250,475);"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.scale(1.0,-1.0);z.strokeStyle=\"#FF0000\";"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.beginPath();if(r.length>0)"));
		plen=fill_tcp_data_p(buf,plen,PSTR("{x=r[0][0]*2*s;y=r[0][1]*s;z.moveTo(x,y);"));
		plen=fill_tcp_data_p(buf,plen,PSTR("for(i=0;i<r.length;i+=1)"));
		plen=fill_tcp_data_p(buf,plen,PSTR("{x=r[i][0]*2*s;y=r[i][1]*s;z.lineTo(x,y);}"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.stroke();}z.restore();"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.font='20px sans-serif';z.textBaseline='top';"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.fillText('"));
		plen=fill_tcp_data_int(buf,plen,power);
		plen=fill_tcp_data_p(buf,plen,PSTR(" W',200,0);z.fillText('2hrs',200,480);"));
		plen=fill_tcp_data_p(buf,plen,PSTR("}}</script></body>"));
        return(plen);
}


// prepare the webpage by writing the data to the tcp send buffer
uint16_t print_webpage_reduced2h(uint8_t *buf, uint16_t *data, uint16_t power)
{       cli(); 
        uint16_t plen;
		uint16_t j;
	  //plen=http200ok();
		plen=fill_tcp_data_p(buf,0,PSTR("HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nPragma: no-cache\r\nRefresh: 2\r\n\r\n"));
	  //uint8_t mydata[10] = {1,4,1,1,6,1,2,1,1,1};
	  //plen=fill_tcp_data_p(buf,plen,PSTR("<pre><script>var p=[1,4,1,1,6,1,2,1,1,1];</script><body onload=\"c(p)\">"));
		plen=fill_tcp_data_p(buf,plen,PSTR("<a href=/s>[1X]</a>"));
		plen=fill_tcp_data_p(buf,plen,PSTR("<a href=/r>[5m]</a>"));
		plen=fill_tcp_data_p(buf,plen,PSTR("<body onload=\"c(p)\"><canvas id=\"g\"width=\"501\"height=\"501\">"));
		plen=fill_tcp_data_p(buf,plen,PSTR("</canvas><script type=\"text/javascript\">var p=["));
		for (j = 0; j< (N_DISP-1); j++){
		_delay_ms(1);
		plen=fill_tcp_data_int(buf,plen,data[j]/10);
		plen=fill_tcp_data_p(buf,plen,PSTR(","));
		}
		plen=fill_tcp_data_int(buf,plen,data[N_DISP-1]/10);
        plen=fill_tcp_data_p(buf,plen,PSTR("];function c(p){var r=[];for(x=-240,i=0;i<=150;i++)"));
        plen=fill_tcp_data_p(buf,plen,PSTR("{y=p[i];r.push([x,y]);x+=3;}d(r);}function d(r)"));
		plen=fill_tcp_data_p(buf,plen,PSTR("{var w=document.getElementById('g');var i,x,y;"));
		plen=fill_tcp_data_p(buf,plen,PSTR("if(w.getContext){var s=2;var z=w.getContext('2d');"));
//		plen=fill_tcp_data_p(buf,plen,PSTR("z.strokeStyle=\"#000000\";z.lineWidth=1;z.beginPath();"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.moveTo(15,15);z.lineTo(15,475);"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.moveTo(15,475);z.lineTo(475,475);"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.stroke();z.save();z.translate(250,475);"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.scale(1.0,-1.0);z.strokeStyle=\"#FF0000\";"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.beginPath();if(r.length>0)"));
		plen=fill_tcp_data_p(buf,plen,PSTR("{x=r[0][0]*0.5*s;y=r[0][1]*s;z.moveTo(x,y);"));
		plen=fill_tcp_data_p(buf,plen,PSTR("for(i=0;i<r.length;i+=1)"));
		plen=fill_tcp_data_p(buf,plen,PSTR("{x=r[i][0]*0.5*s;y=r[i][1]*s;z.lineTo(x,y);}"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.stroke();}z.restore();"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.font='20px sans-serif';z.textBaseline='top';"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.fillText('"));
		plen=fill_tcp_data_int(buf,plen,power);
		plen=fill_tcp_data_p(buf,plen,PSTR(" W',200,0);z.fillText('2hrs',200,480);"));
		plen=fill_tcp_data_p(buf,plen,PSTR("}}</script></body>"));
        return(plen);
}


/*
// prepare the webpage by writing the data to the tcp send buffer
uint16_t print_webpage_reduced5m(uint8_t *buf, uint16_t *data, uint16_t power)
{       cli(); 
        uint16_t plen;
		uint16_t j;
	  //plen=http200ok();
		plen=fill_tcp_data_p(buf,0,PSTR("HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nPragma: no-cache\r\nRefresh: 2\r\n\r\n"));
	  //uint8_t mydata[10] = {1,4,1,1,6,1,2,1,1,1};
	  //plen=fill_tcp_data_p(buf,plen,PSTR("<pre><script>var p=[1,4,1,1,6,1,2,1,1,1];</script><body onload=\"c(p)\">"));
		plen=fill_tcp_data_p(buf,plen,PSTR("<a href=/>[home]</a>\n"));
		plen=fill_tcp_data_p(buf,plen,PSTR("<pre><body onload=\"c(p)\"><canvas id=\"plot\"width=\"501\"height=\"501\">"));
		plen=fill_tcp_data_p(buf,plen,PSTR("</canvas><script>var p=["));
		for (j = 0; j< (N_DISP-1); j++){
		_delay_ms(1);
		plen=fill_tcp_data_int(buf,plen,data[j]/10);
		plen=fill_tcp_data_p(buf,plen,PSTR(","));
		}
		plen=fill_tcp_data_int(buf,plen,data[N_DISP-1]/10);
		plen=fill_tcp_data_p(buf,plen,PSTR("];</script><script type=\"text/javascript\">"));
        plen=fill_tcp_data_p(buf,plen,PSTR("function c(p){var r=[];for(x=-230,i=0;i<=150;i++)"));
        plen=fill_tcp_data_p(buf,plen,PSTR("{y=p[i];r.push([x,y]);x+=3;}d(r);}function d(r)"));
		plen=fill_tcp_data_p(buf,plen,PSTR("{var canvas=document.getElementById('plot');var i,x,y;"));
		plen=fill_tcp_data_p(buf,plen,PSTR("if(canvas.getContext){var s=2;var z=canvas.getContext('2d');"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.strokeStyle=\"#000000\";z.lineWidth=1;z.beginPath();"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.moveTo(15,15);z.lineTo(15,475);"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.moveTo(15,475);z.lineTo(475,475);"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.stroke();z.save();z.translate(250,475);"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.scale(1.0,-1.0);z.strokeStyle=\"#FF0000\";"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.lineWidth=1;z.beginPath();if(r.length>0)"));
		plen=fill_tcp_data_p(buf,plen,PSTR("{x=r[0][0]*0.5*s;y=r[0][1]*s;z.moveTo(x,y);"));
		plen=fill_tcp_data_p(buf,plen,PSTR("for(i=0;i<r.length;i+=1)"));
		plen=fill_tcp_data_p(buf,plen,PSTR("{x=r[i][0]*0.5*s;y=r[i][1]*s;z.lineTo(x,y);}"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.stroke();}z.restore();"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.font='20px sans-serif';z.textBaseline='top';"));
		plen=fill_tcp_data_p(buf,plen,PSTR("z.fillText(' "));
		plen=fill_tcp_data_int(buf,plen,power);
		plen=fill_tcp_data_p(buf,plen,PSTR(" Watts',200,0);z.fillText('last 4 min',200,480);"));
		plen=fill_tcp_data_p(buf,plen,PSTR("}}</script></body></pre>"));
        return(plen);
}

*/

uint16_t print_webpage_dataonly(uint8_t *buf, uint16_t power, uint16_t power1, uint16_t power2, uint16_t pfactor1, uint16_t pfactor2, uint32_t gas_time, uint32_t gas_counter)
{
        uint16_t plen;
        plen=http200ok();	
//		plen=fill_tcp_data_p(buf,0,PSTR("HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nPragma: no-cache\r\nRefresh: 2\r\n\r\n"));
        plen=fill_tcp_data_p(buf,plen,PSTR("P:"));
		plen=fill_tcp_data_int(buf,plen,power); 
		plen=fill_tcp_data_p(buf,plen,PSTR("; P1:"));
		plen=fill_tcp_data_int(buf,plen,power1); 
		plen=fill_tcp_data_p(buf,plen,PSTR("; P2:"));
		plen=fill_tcp_data_int(buf,plen,power2); 
		plen=fill_tcp_data_p(buf,plen,PSTR("; PF1:"));
		plen=fill_tcp_data_int(buf,plen,pfactor1); 
		plen=fill_tcp_data_p(buf,plen,PSTR("; PF2:"));
		plen=fill_tcp_data_int(buf,plen,pfactor2);
		plen=fill_tcp_data_p(buf,plen,PSTR("; GT:"));
		plen=fill_tcp_data_int(buf,plen,gas_time);
        plen=fill_tcp_data_p(buf,plen,PSTR("; GC:"));
		plen=fill_tcp_data_int(buf,plen,gas_counter);                             
        plen=fill_tcp_data_p(buf,plen,PSTR(";\n"));
        return(plen);
}


ISR(ADC_vect)
 { 
 if (ADMUX == 0x00) {
	if (count ==0) GetTime(&t);
		buffer0[count] = ADC;
		ADMUX = 0x01;
 }
 else {
	if (ADMUX == 0x01) {
		buffer1[count] = ADC;
		ADMUX = 0x02;
	}
	else {
		buffer2[count] = ADC;
		ADMUX = 0x00;
		count++;
		//after process last point in 3rd channel, stop conversions	 
		if (count == N_POINTS)  {
			temp =  GetElaspMs(&t);
			temp1 = (UINT32)getSeconds();
			done = 1;
			cli();
			count = 0;
		}
	}	
} 
 ADCSRA |= (1<<ADSC); 
}


int main(void){
        
		channel = 0;
		for (j = 0;j<1000;j++) _delay_ms(5);
		done = 0;		
		
		uint16_t dat_p;        
        // set the clock speed to 8MHz
        // set the clock prescaler. First write CLKPCE to enable setting of clock the
        // next four instructions.
        CLKPR=(1<<CLKPCE);
        CLKPR=0; // 8 MHZ
        _delay_loop_1(0); // 60us
        DDRB|= (1<<DDB1); // LED, enable PB1, LED as output
		DDRB &= ~(1 << PB0);        //PB0 as digital input	
		pulse_high =0;
		counter = 0;
		STATE_IS_LOW_WAITING_FOR_HIGH = 1;
		MAX_COUNTER = 10;
		gas_count = 0;
		
        LEDOFF;
        
        //initialize the hardware driver for the enc28j60
        enc28j60Init(mymac);
        enc28j60clkout(2); // change clkout from 6.25MHz to 12.5MHz
        _delay_loop_1(0); // 60us
        enc28j60PhyWrite(PHLCON,0x476);
        
        //init the ethernet/ip layer:
        init_udp_or_www_server(mymac,myip);
        www_server_port(MYWWWPORT);
		
		// Setup LCD
		lcd_init();
		lcd_contrast(0x36);//0xC8 for 3.3V, ox36 for 5V
		
		
		
		// Print on first line
		lcd_goto_xy(1,1);
		fprintf_P(&lcd_out,PSTR("HOME ENERGY v4"));
		lcd_goto_xy(1,2);
		fprintf_P(&lcd_out,PSTR("P1          kW"));
		lcd_goto_xy(1,3);
		fprintf_P(&lcd_out,PSTR("P2          kW"));
		lcd_goto_xy(1,4);
		fprintf_P(&lcd_out,PSTR("GAS          s"));
		lcd_goto_xy(1,5);
		fprintf_P(&lcd_out,PSTR("PF1"));
		lcd_goto_xy(1,6);
		fprintf_P(&lcd_out,PSTR("PF2"));
			
		//setup ADC
		ADCSRA = _BV(ADEN)|_BV(ADIE)|_BV(ADPS2)|_BV(ADPS1);
		
		
		TimeInit();	//start timer routine
		sei(); 	// enable interrupts
		ADCSRA |= (1<<ADSC); //start conversions
		connect_timer(1);
        last_time = (UINT32)getSeconds();
		
		
		
		//initialize variables
		count = 0;
		overall_counter5m = 0;
		overall_counter2h = 0;
		
		for (j = 0;j<N_WEB;j++) {
		power_vector[j] = 0.0;
		power_vector2h[j] = 0.0;
//		power_vector_old[j] = 0.0;
        } 
		
		for (j = 0;j<N_DISP;j++) {
		power_vector_disp[j] = 0.0;
		power_vector_disp2h[j] = 0.0;
        } 
		
		for (j = 0;j<N_AVG;j++) {
		P1_vec[j] = 0.0;
		VAR1_vec[j]=0.0; 
		VA1_vec[j]=0.0;
		PF1_vec[j]=0.0; 
		P2_vec[j]=0.0; 
		VAR2_vec[j]=0.0; 
		VA2_vec[j]=0.0; 
		PF2_vec[j]=0.0;
        } 

        while(1){
		        overall_counter5m++;
                overall_counter2h++;        
				// conversions completed within ISR 

                if (STATE_IS_LOW_WAITING_FOR_HIGH == 1) {
				/* do stuff with pins*/
				if (PINB & (1<<PB0)) {//pin B measures high
                  if (pulse_high == 0) {//previously in low state, need to change state
                     pulse_high = 1;
					 counter++;
					 }
                   else {//previously in high state, no need to change
				    counter++;
					if (counter > MAX_COUNTER) {//got enough in a row
					    STATE_IS_LOW_WAITING_FOR_HIGH = 0;//state is now high waiting for low
						counter = 0;
						current_time = (UINT32)getSeconds();
						gas_delay_time = current_time - last_time;
						last_time = current_time;
						gas_count++;
					   }
                   }   
				}
                else {//pin B measures low this time
				  if (pulse_high == 0) {//previously in low state, do nothing
					 }
                   else {//previously in high state, need to change state
				    pulse_high = 0;
					counter= 0;
				   }   
                }
                }
				
				else {//state is high waiting for low


				if (PINB & (1<<PB0)) {//pin B measures high
                  if (pulse_high == 0) {//previously in low state, need to change state
                     pulse_high = 1;
                     //reset counter
                     counter = 0; 
					 }
                   else {//previously in high state, do nothing
					   }
                   }   

                else {//pin B measures low this time
				  if (pulse_high == 0) {//previously in low state, no need to change state
					 counter++;  
					    if (counter > MAX_COUNTER) {//got enough in a row
					    STATE_IS_LOW_WAITING_FOR_HIGH = 1;//state is now high waiting for low
						counter = 0;
						//do stuff?
					   }
					 }
                   else {//previously in high state, need to change state
				    pulse_high = 0;
					counter++;		
                   }   
				
                }
				
				}//end state is high waiting for low



				//process data
                vrms = 0.0;
				i1rms = 0.0;
				i2rms = 0.0;
				v_i1 = 0.0;
				v_i2 = 0.0;
 				
			while (done == 0) {};
			
				//subtract all offsets
                for (j = 0;j<N_POINTS;j++) {
				v[j] = (double) buffer0[j];
				i1[j] = (double) buffer1[j];
				i2[j] = (double) buffer2[j];
				}
			     
				//reset the flag 
				done = 0;
				sei();
				 
				//calculate vrms, i1 rms, i2 rms
				//0.004888 = 5/1023: ADC
				//84.62 = (120*2*sqrt(2)/(4.501-.490)): Voltage scaling
				//44.118 = 3000/68: Current sensor
				//1.0902 = 84.6/77.6: broken current sensor recal
				
				
				for (j = 0;j<N_POINTS;j++) {
				vrms = vrms + (0.004888*v[j]-2.5)*(0.004888*v[j]-2.5);
				i1rms = i1rms + 1.0902*(0.004888*i1[j]-2.5)*1.0902*(0.004888*i1[j]-2.5);
				i2rms = i2rms + (0.004888*i2[j]-2.5)*(0.004888*i2[j]-2.5);
				v_i1 = v_i1 + (0.004888*v[j]-2.5)*1.0902*(0.004888*i1[j]-2.5);
				v_i2 = v_i2 + (0.004888*v[j]-2.5)*(0.004888*i2[j]-2.5);
                }
 			     vrms = 84.62*sqrt(vrms/N_POINTS);
				 i1rms = 44.118*sqrt(i1rms/N_POINTS);
				 i2rms = 44.118*sqrt(i2rms/N_POINTS);
				 v_i1 = 84.62*44.118*v_i1/N_POINTS;
				 v_i2 = 84.62*44.118*v_i2/N_POINTS;				
				
				for (j = 0;j<(N_AVG-1);j++){
			    P1_vec[j] = P1_vec[j+1];
				VAR1_vec[j]= VAR1_vec[j+1]; 
				VA1_vec[j]= VA1_vec[j+1];
				PF1_vec[j]= PF1_vec[j+1]; 
				P2_vec[j]=P2_vec[j+1]; 
				VAR2_vec[j]=VAR2_vec[j+1]; 
				VA2_vec[j]=VA2_vec[j+1]; 
				PF2_vec[j]=PF2_vec[j+1];
				}	
				
				P1_vec[N_AVG-1] = fabs(v_i1/1000.0);
				VA1_vec[N_AVG-1] = vrms*i1rms/1000.0;
				VAR1_vec[N_AVG-1] = sqrt(VA1*VA1 - P1*P1);
				PF1_vec[N_AVG-1] = P1/VA1;
				P2_vec[N_AVG-1] = fabs(v_i2/1000.0);
				VA2_vec[N_AVG-1] = vrms*i2rms/1000.0;
				VAR2_vec[N_AVG-1] = sqrt(VA2*VA2 - P2*P2);
				PF2_vec[N_AVG-1] = P2/VA2;

                P1 = P1_vec[0];
				VA1 = VA1_vec[0];
				VAR1 = VAR1_vec[0];
				PF1 = PF1_vec[0];
				P2 = P2_vec[0];
				VA2 = VA2_vec[0];
				VAR2 = VAR2_vec[0];
				PF2 = PF2_vec[0];
				
				for (j = 1;j<(N_AVG);j++){
			    P1 += P1_vec[j];
				VA1 += VA1_vec[j];
				VAR1 += VAR1_vec[j];
				PF1 += PF1_vec[j];
				P2 += P2_vec[j];
				VA2 += VA2_vec[j];
				VAR2 += VAR2_vec[j];
				PF2 += PF2_vec[j];
				}
	 
				//calculate powers
				P1 = P1/(double)N_AVG;
				VA1 = VA1/(double)N_AVG;
				VAR1 = VAR1/(double)N_AVG;
				PF1 = PF1/(double)N_AVG;
				P2 = P2/(double)N_AVG;
				VA2 = VA2/(double)N_AVG;
				VAR2 = VAR2/(double)N_AVG;
				PF2 = PF2/(double)N_AVG;

				lcd_goto_xy(5,2);
				fprintf_P(&lcd_out,PSTR("%-7.3f"),P1);	
				lcd_goto_xy(5,3);
			    fprintf_P(&lcd_out,PSTR("%-7.3f"),P2);	
				lcd_goto_xy(5,4);
                fprintf_P(&lcd_out,PSTR("%-7d"), gas_delay_time);	
				//fprintf_P(&lcd_out,PSTR("%-7d"), temp);	
				
				lcd_goto_xy(5,5);
				fprintf_P(&lcd_out,PSTR("%-5.2f"),PF1);
				lcd_goto_xy(5,6);
				fprintf_P(&lcd_out,PSTR("%-5.2f"),PF2);
				
				
				if (overall_counter5m == 30) {
				
						overall_counter5m = 0;
						for (j = 0;j<(N_WEB-1);j++) {
						power_vector[j] = power_vector[j+1];
						} 
						power_vector[N_WEB-1] = P1 + P2; 
										   
						
						for (j = 0;j<N_WEB;j++) {
						power_vector_disp[j] = (uint16_t)(1000.0*power_vector[j]);
						}
						
						P1_disp = (uint16_t)(1000.0*P1);
						P2_disp = (uint16_t)(1000.0*P2);
						PF1_disp = (uint16_t)(100.0*PF1);
						PF2_disp = (uint16_t)(100.0*PF2);
		        }
				
				if (overall_counter2h == 720) {
				
						overall_counter2h = 0;
						for (j = 0;j<(N_WEB-1);j++) {
						power_vector2h[j] = power_vector2h[j+1];
						} 
						power_vector2h[N_WEB-1] = P1 + P2; 
										   
						
						for (j = 0;j<N_WEB;j++) {
						power_vector_disp2h[j] = (uint16_t)(1000.0*power_vector2h[j]);
						}						
		        }
				
				// read packet, handle ping and wait for a tcp packet:
                dat_p=packetloop_arp_icmp_tcp(buf,enc28j60PacketReceive(BUFFER_SIZE, buf));

                // dat_p will be unequal to zero if there is a valid  http get
                if(dat_p==0){
                        // no http request
                        if (enc28j60linkup()){
                                LEDON;
                        }else{
                                LEDOFF;
                        }
                        continue;
                }
                // tcp port 80 begin
                if (strncmp("GET ",(char *)&(buf[dat_p]),4)!=0){
                        // head, post and other methods:
                        dat_p=http200ok();
                        dat_p=fill_tcp_data_p(buf,dat_p,PSTR("<h1>200 OK</h1>"));
                        goto SENDTCP;
                }
                // just one web page in the "root directory" of the web server
                if (strncmp("/ ",(char *)&(buf[dat_p+4]),2)==0){
                        dat_p=print_webpage5m(buf, power_vector_disp, power_vector_disp[N_WEB-1]);
                        goto SENDTCP;
                }
				if (strncmp("/r",(char *)&(buf[dat_p+4]),2)==0){
						dat_p=print_webpage_reduced5m(buf, power_vector_disp, power_vector_disp[N_WEB-1]);
                        goto SENDTCP;
						}
				if (strncmp("/s",(char *)&(buf[dat_p+4]),2)==0){
						dat_p=print_webpage2h(buf, power_vector_disp2h, power_vector_disp2h[N_WEB-1]);
                        goto SENDTCP;
						}
				if (strncmp("/t",(char *)&(buf[dat_p+4]),2)==0){
						dat_p=print_webpage_reduced2h(buf, power_vector_disp2h, power_vector_disp2h[N_WEB-1]);
                        goto SENDTCP;
						}
				if (strncmp("/c",(char *)&(buf[dat_p+4]),2)==0){
						dat_p=print_webpage_dataonly(buf, power_vector_disp[N_WEB-1], P1_disp, P2_disp, PF1_disp, PF2_disp, gas_delay_time, gas_count);
                        goto SENDTCP;
						}
				  else
				  {
                        dat_p=fill_tcp_data_p(buf,0,PSTR("HTTP/1.0 401 Unauthorized\r\nContent-Type: text/html\r\n\r\n<h1>401 Unauthorized</h1>"));
                        goto SENDTCP;
                }

SENDTCP:
                www_server_reply(buf,dat_p); // send web page data
                // tcp port 80 end
        }
        return (0);
}
