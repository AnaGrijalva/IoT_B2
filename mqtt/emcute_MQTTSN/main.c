#include <stdio.h>
#include <string.h>
#include <stdlib.h>
// libraries added
#include <time.h>

#include "shell.h"
#include "msg.h"
#include "net/emcute.h"
#include "net/ipv6/addr.h"

#ifndef EMCUTE_ID
#define EMCUTE_ID ("gertrud")
#endif
#define EMCUTE_PORT (1883U)
#define EMCUTE_PRIO (THREAD_PRIORITY_MAIN - 1)

static char stack[THREAD_STACKSIZE_DEFAULT];
static msg_t queue[8];

// struct that contains sensors
typedef struct sensors{
  int temperature;
  int humidity;
  int windDirection;
  int windIntensity;
  int rainHeight;
}t_sensors;

static void *emcute_thread(void *arg)
{
    (void)arg;
    emcute_run(EMCUTE_PORT, EMCUTE_ID);
    return NULL; /* should never be reached */
}

// function that disconnects from the mqttsn gateway
static int discon(void){
    int res = emcute_discon();
    if (res == EMCUTE_NOGW) {
        puts("error: no se ha conectado ningun broker");
        return 1;
    }
    else if (res != EMCUTE_OK) {
        puts("error: no es posible desconectar");
        return 1;
    }
    puts("Desconexion exitosa");
    return 0;
}

// function that publish messages to the topic
// it takes as input the topic, the message to send and
// the value of qos
static int pub(char* topic, char* data, int qos){
  emcute_topic_t t;
  unsigned flags = EMCUTE_QOS_0;

  switch (qos) {
      case 1:
        flags |= EMCUTE_QOS_1;
        break;
      case 2:
        flags |= EMCUTE_QOS_2;
        break;
      default:
        flags |= EMCUTE_QOS_0;
        break;
  }



  /* step 1: get topic id */
  t.name = topic;
  if (emcute_reg(&t) != EMCUTE_OK) {
      puts("error: no se obtuvo el topic ID");
      return 1;
  }

  /* step 2: publish data */
  if (emcute_pub(&t, data, strlen(data), flags) != EMCUTE_OK) {
      printf("error: no es posible publicar en el topico '%s [%i]'\n",
              t.name, (int)t.id);
      return 1;
  }

  printf("publicado %s en el topico %s\n", data, topic);

  return 0;
}

// function that connects to the mqtt gateway
// it takes as input the ip address and the port
static int con(char* addr, int port){
  sock_udp_ep_t gw = { .family = AF_INET6, .port = EMCUTE_PORT };
  gw.port = port;

  /* parse address */
  if (ipv6_addr_from_str((ipv6_addr_t *)&gw.addr.ipv6, addr) == NULL) {
      printf("error de direccion ipv6\n");
      return 1;
  }

  if (emcute_con(&gw, true, NULL, NULL, 0, 0) != EMCUTE_OK) {
      printf("error: no es posible conectar con [%s]:%i\n", addr, port);
      return 1;
  }
  printf("Conexion exitosa al gateway [%s]:%i\n", addr, port);
  return 0;
}

// function that computes random values in the specified range
int rand_val(int min, int max){
  srand(time(NULL));
  return (rand() % (int)((max-min+1) * 100)) / 100 + min;
}

// function that generate sensor values
static void gen_sensors_values(t_sensors* sensors){
  sensors->temperatura = rand_val(0,39);
  sensors->humedad = rand_val(10, 100);
  sensors->vientod = rand_val(0, 360);
  sensors->vientoi = rand_val(0, 100);
  sensors->lluvia = rand_val(12, 70);
}

// new shell command: start the station
// the function takes in input ip address and port of the gateway,
// and the id of the specified station
// every five seconds it generates new sensor values and publish them to 
// sensor/station + station id
static int cmd_start(int argc, char **argv){
  if (argc < 4) {
      printf("usage: %s <address> <port> <id>\n", argv[0]);
      return 1;
  }
  // sensors struct
  t_sensors sensors;
  // name of the topic
  char topic[32];
  sprintf(topic,"estaciones/sensores%d", atoi(argv[3]));
  
  // json that it will published
  char json[128];
  
  while(1){
    // it tries to connect to the gateway
    if (con(argv[1], atoi(argv[2]))) {
      continue;
    }
    
    // takes the current date and time
    char datetime[20];
    time_t current;
    time(&current);
    struct tm* t = localtime(&current);
    int c = strftime(datetime, sizeof(datetime), "%Y-%m-%d %T", t);
    if(c == 0) {
      printf("Error formato no valido\n");
      return 0;
    } 

    // updates sensor values
    gen_sensors_values(&sensors);

    // fills the json document
    sprintf(json, "{\"id\": \"%d\", \"datetime\": \"%s\", \"temperatura\": "
                  "\"%d\", \"humedad\": \"%d\", \"vientod\": \"%d\", "
                  "\"vientoi\": \"%d\", \"lluvia\": \"%d\"}",
                  atoi(argv[3]), datetime, sensors.temperatura, sensors.humedad, 
                  sensors.vientod, sensors.vientoi, sensors.lluvia);
      
    // publish to the topic
    pub(topic, json, 0);

    // it disconnects from the gateway
    discon();

    // it sleeps for five seconds
    xtimer_sleep(5);
  }

  return 0;
}

static const shell_command_t shell_commands[] = {
    {"empezar", "Inicio de la estacion", cmd_start},
    {NULL, NULL, NULL}};

int main(void)
{
    puts("MQTT-SN application\n");
    puts("Type 'help' to get started. Have a look at the README.md for more "
         "information.");

    /* the main thread needs a msg queue to be able to run `ping6`*/
    msg_init_queue(queue, ARRAY_SIZE(queue));

    /* start the emcute thread */
    thread_create(stack, sizeof(stack), EMCUTE_PRIO, 0,
                  emcute_thread, NULL, "emcute");

    /* start shell */
    char line_buf[SHELL_DEFAULT_BUFSIZE];
    shell_run(shell_commands, line_buf, SHELL_DEFAULT_BUFSIZE);

    /* should be never reached */
    return 0;
}
