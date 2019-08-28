#include <stdio.h>
#include <stdlib.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<termios.h>
#include<errno.h>
#include<string.h>
#include<stdbool.h>
#include<time.h>
#include<event.h>
#include<event2/util.h>
#include <json/json.h>

#define CHANNEL_NAME_LEN 32
#define CHANNEL_VALUE_LEN 64
#define MAX_CHANNELS 32
#define TIMER_INTERVAL 10

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

struct channel_info {
    char name[CHANNEL_NAME_LEN];
    char value[CHANNEL_VALUE_LEN];
    bool valid;
    bool has_data;
}g_channels[MAX_CHANNELS];

static int uart_open(char* port)
{
    int fd;

    fd = open(port, O_RDWR|O_NOCTTY|O_NDELAY);
    if(fd < 0){
        fprintf(stderr, "Can't Open Serial Port: %s\n", port);
        return -1;
    }

    return fd;
}

static void uart_close(int fd)
{
	close(fd);
}

#if 0
/* need to debug with real serial port... */
static int uart_setopt(int fd,int speed,int bits,char event,int stop)
{
    struct termios newtio,oldtio;

    if(tcgetattr(fd,&oldtio)!=0) {
        fprintf(stderr, "tcgetattr fail\n");
        return -1;
    }

	bzero(&newtio,sizeof(newtio));
    newtio.c_cflag |= CLOCAL | CREAD;
    newtio.c_cflag &= ~CSIZE;
    newtio.c_lflag |=ICANON; //标准模式 
    
    switch(bits) {
        case 7:
            newtio.c_cflag |= CS7;
            break;
        case 8:
            newtio.c_cflag |= CS8;
            break;
        default:
            newtio.c_cflag |= CS8;
            break;    
    }

    switch(event) {
        case 'O':
            newtio.c_cflag |= PARENB;
            newtio.c_cflag |= PARODD;
            newtio.c_iflag |= (INPCK | ISTRIP);
            break;
        case 'E':
            newtio.c_iflag |= (INPCK | ISTRIP);
            newtio.c_cflag |= PARENB;
            newtio.c_cflag &= ~PARODD;
            break;
        case 'N':
            newtio.c_cflag &= ~PARENB;
            break;
        default:
            newtio.c_cflag &= ~PARENB;
            break;    
    }

    switch(speed) {
        case 2400:
            cfsetispeed(&newtio, B2400);
            cfsetospeed(&newtio, B2400);
            break;
        case 4800:
            cfsetispeed(&newtio, B4800);
            cfsetospeed(&newtio, B4800);
            break;
        case 9600:
            cfsetispeed(&newtio, B9600);
            cfsetospeed(&newtio, B9600);
            break;
        case 115200:
            cfsetispeed(&newtio, B115200);
            cfsetospeed(&newtio, B115200);
            break;
        case 460800:
            cfsetispeed(&newtio, B460800);
            cfsetospeed(&newtio, B460800);
            break;
        default:
            cfsetispeed(&newtio, B9600);
            cfsetospeed(&newtio, B9600);
            break;
    }

    if(stop == 1)
        newtio.c_cflag &= ~CSTOPB;
    else if(stop == 2)
        newtio.c_cflag |= CSTOPB;

    newtio.c_cc[VTIME] = 1;
    newtio.c_cc[VMIN] = 0;
    tcflush(fd,TCIFLUSH);

    if(tcsetattr(fd,TCSANOW,&newtio)!=0) {
        fprintf(stderr, "tcsetattr fail\n");
        return -1;
    }

    return 0;
}
#else
/* simulate... */
static int uart_setopt(int fd,int speed,int bits,char event,int stop)
{
    struct termios opt;

    tcgetattr(fd,&opt);
    cfmakeraw(&opt);
    cfsetispeed(&opt,B9600);
    cfsetospeed(&opt,B9600);
    tcsetattr(fd, TCSANOW,&opt);

    return 0;
}
#endif

static int uart_init(char* port)
{
    int fd;

    fd = uart_open(port);
    if(fd < 0)
        return -1;
    if(uart_setopt(fd, 2400, 8, 'N', 1) < 0) {
        uart_close(fd);
        return -1;
    }

    return fd;
}

static void fill_channel_info(char* msg)
{
    char * pch;
    int i = 0;
    char* ch_name;
    char* ch_value;
    struct channel_info* channel;

    /* assuming the format... */
    pch = strtok (msg,"\r\n");
    while (pch != NULL) {
        ch_value = strstr(pch, ":");
        if(ch_value) {
            ch_name = ch_value - 1;
            channel = &g_channels[i++];
            if(i >= ARRAY_SIZE(g_channels))
                return;
            ch_value++;
            while(*ch_value == ' ' && *ch_value != '\0')
                ch_value++;
            strncpy(channel->value, *ch_value == '\0'? "": ch_value, sizeof(channel->value));
            while(*ch_name == ' '&& ch_name >= pch)
                ch_name--;
            if(ch_name >= pch) {
                *(++ch_name) = '\0';
                strncpy(channel->name, pch, sizeof(channel->name));
            } else
                strncpy(channel->name, "", sizeof(channel->name));

            channel->has_data = true;
        }
        pch = strtok (NULL, "\r\n");
  }
}

static void add_time_info(json_object* jo)
{
    char *wday[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    time_t timep;
    struct tm *p_tm;
    char time_info[64];
    
    timep = time(NULL);
    p_tm = localtime(&timep);
    snprintf(time_info, sizeof(time_info),"%d-%d-%d %s %d:%d:%d",
            p_tm->tm_year + 1900, p_tm->tm_mon + 1, p_tm->tm_mday,
            wday[p_tm->tm_wday], p_tm->tm_hour, p_tm->tm_min, p_tm->tm_sec);
    json_object_object_add(jo, "TIME", json_object_new_string(time_info));
}

/* dump in JSON just as the following:
  {
    "TIME":"2019-8-29 Thu 20:19:20",
    "A":"5300 Kg",
    "B":"17300 Kg",
    "C":"22300 Kg",
    "D":"15300 Kg",
    "TOTAL":"60200 Kg",
    "VALID":"true"
}*/
static void print_channels_json()
{
    int i;
    struct channel_info* channel;
    json_object* jo_channels = NULL;
    int total = 0;
    bool is_valid = false;
    jo_channels = json_object_new_object();
    if(!jo_channels)
        return;

    /* output time too */
    add_time_info(jo_channels);

    for(i = 0; i < ARRAY_SIZE(g_channels); i++) {
        channel = &g_channels[i];
        if(channel->has_data) {
            json_object_object_add(jo_channels, channel->name, json_object_new_string(channel->value));
            if(!strcmp(channel->name, "TOTAL")) {
                is_valid = (atoi(channel->value) == total)? true: false;
                json_object_object_add(jo_channels, "VALID",
                                       is_valid? json_object_new_string("true"): json_object_new_string("false"));
            }
            else
                total += atoi(channel->value);

            /* invalidate */
            channel->has_data = false;
        }
    }

    printf("%s\n", json_object_to_json_string_ext(jo_channels, JSON_C_TO_STRING_PRETTY));
    json_object_put(jo_channels);
}

static void serial_read_cb(int fd, short events, void *arg)
{
    char msg[2048];
    int len;

    len = read(fd, msg, sizeof(msg) - 1);
    if( len <= 0 ) {
        fprintf(stderr, "Fail to read\n");
        return;
    }
 
    msg[len] = '\0';
    fill_channel_info(msg);
}

static void regular_timer_cb(evutil_socket_t fd, short what, void *arg)
{   
    print_channels_json();
}

static void delay_timer_cb(evutil_socket_t fd, short what, void *arg)
{
    struct event *ev_timer_regular = (struct event* )arg;
    struct timeval interval = {TIMER_INTERVAL, 0};

    print_channels_json();
    /* fire the regular timer, the delay_time itself will not run any longer */
    event_add(ev_timer_regular, &interval);
}

int main(int argc, char *argv[])
{   
    int fd;
    struct event_base* base;
    struct event *ev_serial;
    struct event *ev_timer_regular;
    struct event *ev_timer_delay;
    time_t timep;
    struct tm *p_tm;
    int delay;
    struct timeval interval = {TIMER_INTERVAL, 0};
    struct timeval delay_time = {0, 0};

    if(argc != 2){
        printf("Usage: %s port_name \n",argv[0]);
        exit(1);
    }

    /* open & config serial port */
    fd = uart_init(argv[1]);
    if(fd < 0)
        exit(1);

    /* add serial port fd into event base */
    base = event_base_new();
    ev_serial = event_new(base, fd, EV_READ | EV_PERSIST, serial_read_cb, NULL);
    event_add(ev_serial, NULL);

    /* add 10 seconds timer(regular_timer) into event base */
    ev_timer_regular = event_new(base, -1, EV_PERSIST, regular_timer_cb, NULL);
    timep = time(NULL);
    p_tm = localtime(&timep);
    delay = 10 - p_tm->tm_sec % 10; /* need to align to 10 second */
    if(delay) {
        /* this timer(delay_timer) run only once: it will trigger the regular timer */
        ev_timer_delay = event_new(base, -1, 0, delay_timer_cb, ev_timer_regular);
        delay_time.tv_sec = delay;
        event_add(ev_timer_delay, &delay_time);
    }
    else
        event_add(ev_timer_regular, &interval); /* no need to delay */

    event_base_dispatch(base);

    event_base_free(base);

    return 0;
}