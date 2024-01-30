#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <bcm2835.h>
#include "cJSON.h"

#define GREEN_LIGHT 'G'
#define RED_LIGHT 'R'
#define YELLOW_LIGHT 'Y'
#define OFF_LIGHT 'O'

#define NIGHT_MODE 'N'
#define DEFAULT_MODE 'D'
#define EMERGENCY_MODE 'E'

#define MIN_GREEN_AUX_TIME 5
#define MIN_RED_AUX_TIME 10 + 2
#define MAX_GREEN_AUX_TIME 10
#define MAX_RED_AUX_TIME 20 + 2
#define MIN_GREEN_MAIN_TIME 10
#define MIN_RED_MAIN_TIME 5 + 2
#define MAX_GREEN_MAIN_TIME 20
#define MAX_RED_MAIN_TIME 10 + 2
#define YELLOW_TIME 2

#define MAX_MAIN_SPEED 80
#define MAX_AUX_SPEED 60
#define MIN_STOPPED_TIME_MS 500
#define CAR_DISTANCE 2.0

#define MAX_MESSAGE_SIZE 1024

#define SEND_CENTRAL_INFO_TIME 2

#define MAX_TEMP_SPEEDS 100

struct PinsConfig
{
    int TRAFFIC_LIGHT_MAIN_PIN_1;
    int TRAFFIC_LIGHT_MAIN_PIN_2;
    int TRAFFIC_LIGHT_AUX_PIN_1;
    int TRAFFIC_LIGHT_AUX_PIN_2;
    int CROSSWALK_BUTTON_1;
    int CROSSWALK_BUTTON_2;
    int SENSOR_AUX_1;
    int SENSOR_AUX_2;
    int SENSOR_MAIN_1;
    int SENSOR_MAIN_2;
    int BUZZER;
};

struct TrafficViolationsTemp
{
    int exceeded_limit_speed_count;
    int red_light_violation_count;
};

struct TempTrafficSpeeds
{
    double main_east_speeds[MAX_TEMP_SPEEDS];
    double main_west_speeds[MAX_TEMP_SPEEDS];
    double aux_north_speeds[MAX_TEMP_SPEEDS];
    double aux_south_speeds[MAX_TEMP_SPEEDS];
    int main_east_temp_count;
    int main_west_temp_count;
    int aux_north_temp_count;
    int aux_south_temp_count;
};

struct TrafficLight
{
    char name[20];
    char current_color;
    int current_time;
    bool pedestrian_waiting;
    bool car_waiting;
};

struct TrafficLightTimes
{
    char type[5];
    int green_aux_time;
    int red_aux_time;
    int green_main_time;
    int red_main_time;
    int yellow_time;
};

struct BuzzerArgs
{
    int on_off;
};

struct TempTrafficSpeeds tempSpeeds;
struct TrafficLight trafficLightMain;
struct TrafficLight trafficLightAux;
struct TrafficLightTimes trafficLightTimes;
struct TrafficViolationsTemp trafficViolationsTemp;
struct PinsConfig pinsConfig;
char trafficLightMode;
int intersection_id;
int sockfd;

void define_pins(int intersection_id)
{
    char filepath[100];
    snprintf(filepath, sizeof(filepath), "config/pins-%d.json", intersection_id);

    FILE *file = fopen(filepath, "r");

    if (file == NULL)
    {
        fprintf(stderr, "Não foi possível abrir o arquivo JSON.\n");
        return;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);

    char *jsonStr = (char *)malloc(file_size + 1);

    if (jsonStr == NULL)
    {
        fprintf(stderr, "Erro de alocação de memória.\n");
        fclose(file);
        return;
    }

    if (fread(jsonStr, 1, file_size, file) != (size_t)file_size)
    {
        fprintf(stderr, "Erro ao ler o arquivo JSON.\n");
        free(jsonStr);
        fclose(file);
        return;
    }

    fclose(file);
    jsonStr[file_size] = '\0';

    cJSON *root = cJSON_Parse(jsonStr);

    if (root == NULL)
    {
        fprintf(stderr, "Erro ao analisar o JSON.\n");
        free(jsonStr);
        return;
    }

    pinsConfig.TRAFFIC_LIGHT_MAIN_PIN_1 = cJSON_GetObjectItem(root, "TRAFFIC_LIGHT_MAIN_PIN_1")->valueint;
    pinsConfig.TRAFFIC_LIGHT_MAIN_PIN_2 = cJSON_GetObjectItem(root, "TRAFFIC_LIGHT_MAIN_PIN_2")->valueint;
    pinsConfig.TRAFFIC_LIGHT_AUX_PIN_1 = cJSON_GetObjectItem(root, "TRAFFIC_LIGHT_AUX_PIN_1")->valueint;
    pinsConfig.TRAFFIC_LIGHT_AUX_PIN_2 = cJSON_GetObjectItem(root, "TRAFFIC_LIGHT_AUX_PIN_2")->valueint;
    pinsConfig.CROSSWALK_BUTTON_1 = cJSON_GetObjectItem(root, "CROSSWALK_BUTTON_1")->valueint;
    pinsConfig.CROSSWALK_BUTTON_2 = cJSON_GetObjectItem(root, "CROSSWALK_BUTTON_2")->valueint;
    pinsConfig.SENSOR_AUX_1 = cJSON_GetObjectItem(root, "SENSOR_AUX_1")->valueint;
    pinsConfig.SENSOR_AUX_2 = cJSON_GetObjectItem(root, "SENSOR_AUX_2")->valueint;
    pinsConfig.SENSOR_MAIN_1 = cJSON_GetObjectItem(root, "SENSOR_MAIN_1")->valueint;
    pinsConfig.SENSOR_MAIN_2 = cJSON_GetObjectItem(root, "SENSOR_MAIN_2")->valueint;
    pinsConfig.BUZZER = cJSON_GetObjectItem(root, "BUZZER")->valueint;

    cJSON_Delete(root);
    free(jsonStr);
}

void set_pins()
{
    bcm2835_gpio_fsel(pinsConfig.SENSOR_MAIN_1, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_fsel(pinsConfig.SENSOR_MAIN_2, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_fsel(pinsConfig.SENSOR_AUX_1, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_fsel(pinsConfig.SENSOR_AUX_2, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_fsel(pinsConfig.CROSSWALK_BUTTON_1, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_fsel(pinsConfig.CROSSWALK_BUTTON_2, BCM2835_GPIO_FSEL_INPT);

    bcm2835_gpio_set_pud(pinsConfig.SENSOR_MAIN_1, BCM2835_GPIO_PUD_DOWN);
    bcm2835_gpio_set_pud(pinsConfig.SENSOR_MAIN_2, BCM2835_GPIO_PUD_DOWN);
    bcm2835_gpio_set_pud(pinsConfig.SENSOR_AUX_1, BCM2835_GPIO_PUD_DOWN);
    bcm2835_gpio_set_pud(pinsConfig.SENSOR_AUX_2, BCM2835_GPIO_PUD_DOWN);
    bcm2835_gpio_set_pud(pinsConfig.CROSSWALK_BUTTON_1, BCM2835_GPIO_PUD_DOWN);
    bcm2835_gpio_set_pud(pinsConfig.CROSSWALK_BUTTON_2, BCM2835_GPIO_PUD_DOWN);

    bcm2835_gpio_fsel(pinsConfig.TRAFFIC_LIGHT_MAIN_PIN_1, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(pinsConfig.TRAFFIC_LIGHT_MAIN_PIN_2, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(pinsConfig.TRAFFIC_LIGHT_AUX_PIN_1, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(pinsConfig.TRAFFIC_LIGHT_AUX_PIN_2, BCM2835_GPIO_FSEL_OUTP);
}

void handle_interruption(int sinal)
{
    bcm2835_close();
    exit(0);
}

void set_min_traffic_lights_time()
{
    printf("Set Time: MIN\n");
    snprintf(trafficLightTimes.type, sizeof(trafficLightTimes.type), "Min");
    trafficLightTimes.red_aux_time = MIN_RED_AUX_TIME;
    trafficLightTimes.green_aux_time = MIN_GREEN_AUX_TIME;
    trafficLightTimes.red_main_time = MIN_RED_MAIN_TIME;
    trafficLightTimes.green_main_time = MIN_GREEN_MAIN_TIME;
    trafficLightTimes.yellow_time = YELLOW_TIME;
}

void set_max_traffic_lights_time()
{
    printf("Set Time: MAX\n");
    snprintf(trafficLightTimes.type, sizeof(trafficLightTimes.type), "Max");
    trafficLightTimes.red_aux_time = MAX_RED_AUX_TIME;
    trafficLightTimes.green_aux_time = MAX_GREEN_AUX_TIME;
    trafficLightTimes.red_main_time = MAX_RED_MAIN_TIME;
    trafficLightTimes.green_main_time = MAX_GREEN_MAIN_TIME;
    trafficLightTimes.yellow_time = YELLOW_TIME;
}

void handle_traffic_light_main(char light_color)
{
    if (light_color == RED_LIGHT)
    {
        trafficLightMain.current_color = RED_LIGHT;
        bcm2835_gpio_write(pinsConfig.TRAFFIC_LIGHT_MAIN_PIN_1, 1);
        bcm2835_gpio_write(pinsConfig.TRAFFIC_LIGHT_MAIN_PIN_2, 1);
    }
    else if (light_color == GREEN_LIGHT)
    {
        trafficLightMain.current_color = GREEN_LIGHT;
        bcm2835_gpio_write(pinsConfig.TRAFFIC_LIGHT_MAIN_PIN_1, 0);
        bcm2835_gpio_write(pinsConfig.TRAFFIC_LIGHT_MAIN_PIN_2, 1);
    }
    else if (light_color == YELLOW_LIGHT)
    {
        trafficLightMain.current_color = YELLOW_LIGHT;
        bcm2835_gpio_write(pinsConfig.TRAFFIC_LIGHT_MAIN_PIN_1, 1);
        bcm2835_gpio_write(pinsConfig.TRAFFIC_LIGHT_MAIN_PIN_2, 0);
    }
    else if (light_color == OFF_LIGHT)
    {
        trafficLightMain.current_color = OFF_LIGHT;
        bcm2835_gpio_write(pinsConfig.TRAFFIC_LIGHT_MAIN_PIN_1, 0);
        bcm2835_gpio_write(pinsConfig.TRAFFIC_LIGHT_MAIN_PIN_2, 0);
    }
    trafficLightMain.current_time = 0;
}

void handle_traffic_light_aux(char light_color)
{
    if (light_color == RED_LIGHT)
    {
        trafficLightAux.current_color = RED_LIGHT;
        bcm2835_gpio_write(pinsConfig.TRAFFIC_LIGHT_AUX_PIN_1, 1);
        bcm2835_gpio_write(pinsConfig.TRAFFIC_LIGHT_AUX_PIN_2, 1);
    }
    else if (light_color == GREEN_LIGHT)
    {
        trafficLightAux.current_color = GREEN_LIGHT;
        bcm2835_gpio_write(pinsConfig.TRAFFIC_LIGHT_AUX_PIN_1, 0);
        bcm2835_gpio_write(pinsConfig.TRAFFIC_LIGHT_AUX_PIN_2, 1);
    }
    else if (light_color == YELLOW_LIGHT)
    {
        trafficLightAux.current_color = YELLOW_LIGHT;
        bcm2835_gpio_write(pinsConfig.TRAFFIC_LIGHT_AUX_PIN_1, 1);
        bcm2835_gpio_write(pinsConfig.TRAFFIC_LIGHT_AUX_PIN_2, 0);
    }
    else if (light_color == OFF_LIGHT)
    {
        trafficLightAux.current_color = OFF_LIGHT;
        bcm2835_gpio_write(pinsConfig.TRAFFIC_LIGHT_AUX_PIN_1, 0);
        bcm2835_gpio_write(pinsConfig.TRAFFIC_LIGHT_AUX_PIN_2, 0);
    }
    trafficLightAux.current_time = 0;
}

void handle_traffic_light_night_mode()
{
    printf("Night Mode\n");
    bool lastActive = false;
    while (trafficLightMode == NIGHT_MODE)
    {
        if (!lastActive)
        {
            handle_traffic_light_main(YELLOW_LIGHT);
            handle_traffic_light_aux(YELLOW_LIGHT);
            handle_traffic_light_main(OFF_LIGHT);
            handle_traffic_light_aux(OFF_LIGHT);
            lastActive = true;
        }
        else
        {
            handle_traffic_light_main(OFF_LIGHT);
            handle_traffic_light_aux(OFF_LIGHT);
            handle_traffic_light_main(YELLOW_LIGHT);
            handle_traffic_light_aux(YELLOW_LIGHT);
            lastActive = false;
        }
        sleep(1.5);
    }
}

void handle_traffic_light_emergency_mode()
{
    printf("Emergency Mode\n");
    while (trafficLightMode == EMERGENCY_MODE)
    {
        handle_traffic_light_main(GREEN_LIGHT);
        handle_traffic_light_aux(RED_LIGHT);
        sleep(1);
    }
}

void *buzzer_thread_handle()
{
    bcm2835_gpio_write(pinsConfig.BUZZER, 1);
    bcm2835_delay(1000);
    bcm2835_gpio_write(pinsConfig.BUZZER, 0);
    return NULL;
}

void activate_buzzer()
{
    pthread_t traffic_lights_thread;
    pthread_create(&traffic_lights_thread, NULL, buzzer_thread_handle, NULL);
}

void handle_traffic_light_default_mode()
{
    printf("Default Mode\n");
    while (trafficLightMode == DEFAULT_MODE)
    {
        if (trafficLightMain.current_color == RED_LIGHT &&
            trafficLightAux.current_color == GREEN_LIGHT &&
            trafficLightAux.current_time >= trafficLightTimes.green_aux_time)
        {
            handle_traffic_light_aux(YELLOW_LIGHT);
            activate_buzzer();
        }
        if (trafficLightAux.current_color == RED_LIGHT &&
            trafficLightMain.current_color == GREEN_LIGHT &&
            trafficLightMain.current_time >= trafficLightTimes.green_main_time)
        {
            handle_traffic_light_main(YELLOW_LIGHT);
            activate_buzzer();
        }
        if (trafficLightMain.current_color == RED_LIGHT &&
            trafficLightAux.current_color == YELLOW_LIGHT &&
            trafficLightAux.current_time >= trafficLightTimes.yellow_time)
        {
            handle_traffic_light_main(GREEN_LIGHT);
            handle_traffic_light_aux(RED_LIGHT);
            if (trafficLightAux.pedestrian_waiting || trafficLightAux.car_waiting)
            {
                trafficLightAux.pedestrian_waiting = false;
                trafficLightAux.car_waiting = false;
                set_max_traffic_lights_time();
            }
        }
        if (trafficLightAux.current_color == RED_LIGHT &&
            trafficLightMain.current_color == YELLOW_LIGHT &&
            trafficLightMain.current_time >= trafficLightTimes.yellow_time)
        {
            handle_traffic_light_aux(GREEN_LIGHT);
            handle_traffic_light_main(RED_LIGHT);
            if (trafficLightMain.pedestrian_waiting)
            {
                trafficLightMain.pedestrian_waiting = false;
                trafficLightMain.car_waiting = false;
                set_max_traffic_lights_time();
            }
        }

        trafficLightMain.current_time += 1;
        trafficLightAux.current_time += 1;
        sleep(1);
    }
}

void handle_all_traffic_lights()
{
    while (1)
    {
        if (trafficLightMode == EMERGENCY_MODE)
            handle_traffic_light_emergency_mode();
        else if (trafficLightMode == NIGHT_MODE)
            handle_traffic_light_night_mode();
        else if (trafficLightMode == DEFAULT_MODE)
            handle_traffic_light_default_mode();
    }
}

void handle_crosswalk_button(int pin)
{
    if (pin == pinsConfig.CROSSWALK_BUTTON_1 &&
        trafficLightMain.current_color != RED_LIGHT &&
        trafficLightMain.pedestrian_waiting == false)
    {
        trafficLightMain.pedestrian_waiting = true;
        printf("Pedestrian Waiting\n");
        set_min_traffic_lights_time();
    }
    else if (pin == pinsConfig.CROSSWALK_BUTTON_2 &&
             trafficLightAux.current_color != RED_LIGHT &&
             trafficLightAux.pedestrian_waiting == false)
    {
        printf("Pedestrian Waiting\n");
        trafficLightAux.pedestrian_waiting = true;
        set_min_traffic_lights_time();
    }
}

void handle_all_buttons()
{
    int lastCrosswalkButton1State = LOW, lastCrosswalkButton2State = LOW;
    int currentCrosswalkButton1State, currentCrosswalkButton2State;
    while (1)
    {
        currentCrosswalkButton1State = bcm2835_gpio_lev(pinsConfig.CROSSWALK_BUTTON_1);
        if (currentCrosswalkButton1State == HIGH && lastCrosswalkButton1State == LOW)
            handle_crosswalk_button(pinsConfig.CROSSWALK_BUTTON_1);
        lastCrosswalkButton1State = currentCrosswalkButton1State;

        currentCrosswalkButton2State = bcm2835_gpio_lev(pinsConfig.CROSSWALK_BUTTON_2);
        if (currentCrosswalkButton2State == HIGH && lastCrosswalkButton2State == LOW)
            handle_crosswalk_button(pinsConfig.CROSSWALK_BUTTON_2);
        lastCrosswalkButton2State = currentCrosswalkButton2State;

        bcm2835_delay(100);
    }
}

unsigned long current_time_in_ms()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned long)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

void handle_traffic_statistics(int pin, double speed)
{
    if (pin == pinsConfig.SENSOR_AUX_1)
    {
        tempSpeeds.aux_north_speeds[tempSpeeds.aux_north_temp_count] = speed;
        tempSpeeds.aux_north_temp_count += 1;
    }
    else if (pin == pinsConfig.SENSOR_AUX_2)
    {
        tempSpeeds.aux_south_speeds[tempSpeeds.aux_south_temp_count] = speed;
        tempSpeeds.aux_south_temp_count += 1;
    }
    else if (pin == pinsConfig.SENSOR_MAIN_1)
    {
        tempSpeeds.main_west_speeds[tempSpeeds.main_east_temp_count] = speed;
        tempSpeeds.main_west_temp_count += 1;
    }
    else if (pin == pinsConfig.SENSOR_AUX_2)
    {
        tempSpeeds.main_east_speeds[tempSpeeds.main_east_temp_count] = speed;
        tempSpeeds.main_east_temp_count += 1;
    }
}

void handle_cars_violation(int pin, double speed)
{
    if ((pin == pinsConfig.SENSOR_MAIN_1 || pin == pinsConfig.SENSOR_MAIN_2) && (speed > MAX_MAIN_SPEED))
    {
        activate_buzzer();
        trafficViolationsTemp.exceeded_limit_speed_count += 1;
    }
    else if ((pin == pinsConfig.SENSOR_AUX_1 || pin == pinsConfig.SENSOR_AUX_2) && (speed > MAX_AUX_SPEED))
    {
        activate_buzzer();
        trafficViolationsTemp.exceeded_limit_speed_count += 1;
    }

    if ((pin == pinsConfig.SENSOR_MAIN_1 || pin == pinsConfig.SENSOR_MAIN_2) && trafficLightMain.current_color == RED_LIGHT)
    {
        activate_buzzer();
        trafficViolationsTemp.red_light_violation_count += 1;
    }
    else if ((pin == pinsConfig.SENSOR_AUX_1 || pin == pinsConfig.SENSOR_AUX_2) && trafficLightAux.current_color == RED_LIGHT)
    {
        activate_buzzer();
        trafficViolationsTemp.red_light_violation_count += 1;
    }
}

void handle_sensor(unsigned long time, int pin)
{
    double speed = CAR_DISTANCE / (time / 1000.0) * 3.6;

    printf("speed: %lf\n", speed);

    handle_traffic_statistics(pin, speed);
    handle_cars_violation(pin, speed);
}

void handle_stopped_car(int pin)
{
    if ((pin == pinsConfig.SENSOR_MAIN_1 || pin == pinsConfig.SENSOR_MAIN_2) &&
        trafficLightMain.current_color == RED_LIGHT &&
        trafficLightMain.car_waiting == false)
    {
        printf("STOPPED\n");
        trafficLightMain.car_waiting = true;
        set_min_traffic_lights_time();
    }
    else if ((pin == pinsConfig.SENSOR_AUX_1 || pin == pinsConfig.SENSOR_AUX_2) &&
             trafficLightAux.current_color == RED_LIGHT &&
             trafficLightAux.car_waiting == false)
    {
        printf("STOPPED\n");
        trafficLightAux.car_waiting = true;
        set_min_traffic_lights_time();
    }
}

void handle_sensor_traffic_light_time(int pin)
{
    if ((pin == pinsConfig.SENSOR_AUX_1 || pin == pinsConfig.SENSOR_AUX_2) &&
        trafficLightMain.current_color == RED_LIGHT &&
        trafficLightMain.car_waiting == false)
    {
        trafficLightMain.car_waiting = true;
        printf("Pedestrian Waiting\n");
        set_min_traffic_lights_time();
    }
    else if ((pin == pinsConfig.SENSOR_MAIN_1 || pin == pinsConfig.SENSOR_MAIN_2) &&
             trafficLightAux.current_color == RED_LIGHT &&
             trafficLightAux.car_waiting == false)
    {
        printf("Pedestrian Waiting\n");
        trafficLightAux.car_waiting = true;
        set_min_traffic_lights_time();
    }
}

void handle_sensor_timing(int pin, unsigned long *startTime, int *lastState, bool *car_waiting)
{
    int state = bcm2835_gpio_lev(pin);
    unsigned long currentTime = current_time_in_ms();

    if (state == HIGH && *lastState == LOW)
        *startTime = currentTime;
    if (state == LOW && *lastState == HIGH)
    {
        handle_sensor(currentTime - *startTime, pin);
        *startTime = 0;
    }
    if (*startTime != 0 && (currentTime - *startTime) > MIN_STOPPED_TIME_MS && !(*car_waiting))
    {
        handle_stopped_car(pin);
    }

    *lastState = state;
}

void handle_all_sensors()
{
    int lastStateMain1 = LOW, lastStateMain2 = LOW, lastStateAux1 = LOW, lastStateAux2 = LOW;
    unsigned long startTimeMain1, startTimeMain2, startTimeAux1, startTimeAux2;
    startTimeMain1 = startTimeMain2 = startTimeAux1 = startTimeAux2 = 0;
    while (1)
    {
        handle_sensor_timing(pinsConfig.SENSOR_MAIN_1, &startTimeMain1, &lastStateMain1, &trafficLightMain.car_waiting);
        handle_sensor_timing(pinsConfig.SENSOR_MAIN_2, &startTimeMain2, &lastStateMain2, &trafficLightMain.car_waiting);
        handle_sensor_timing(pinsConfig.SENSOR_AUX_1, &startTimeAux1, &lastStateAux1, &trafficLightAux.car_waiting);
        handle_sensor_timing(pinsConfig.SENSOR_AUX_2, &startTimeAux2, &lastStateAux2, &trafficLightAux.car_waiting);

        bcm2835_delay(10);
    }
}

void init_states()
{
    handle_traffic_light_main(GREEN_LIGHT);
    handle_traffic_light_aux(RED_LIGHT);
    trafficLightMain.pedestrian_waiting = false;
    trafficLightAux.pedestrian_waiting = false;
    snprintf(trafficLightMain.name, sizeof(trafficLightMain.name), "Main Traffic Light");
    snprintf(trafficLightAux.name, sizeof(trafficLightAux.name), "Aux Traffic Light");
    set_max_traffic_lights_time();
    trafficViolationsTemp.exceeded_limit_speed_count = 0;
    trafficViolationsTemp.red_light_violation_count = 0;
    tempSpeeds.aux_north_temp_count = 0;
    tempSpeeds.aux_south_temp_count = 0;
    tempSpeeds.main_east_temp_count = 0;
    tempSpeeds.main_west_temp_count = 0;
    trafficLightMode = DEFAULT_MODE;
}

void handle_central_server_command(char command)
{
    if (command == NIGHT_MODE)
        trafficLightMode = NIGHT_MODE;
    else if (command == EMERGENCY_MODE)
        trafficLightMode = EMERGENCY_MODE;
    else if (command == DEFAULT_MODE)
        trafficLightMode = DEFAULT_MODE;
}

void handle_recive_central_message()
{
    char buffer[1024];
    while (1)
    {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytesReceived = recv(sockfd, buffer, sizeof(buffer), 0);
        if (bytesReceived < 0)
        {
            sleep(1);
            continue;
        }
        else if (bytesReceived == 0)
        {
            printf("Conexão encerrada pelo servidor.\n");
            sleep(1);
            continue;
        }

        cJSON *json = cJSON_Parse(buffer);
        if (json == NULL)
        {
            fprintf(stderr, "Erro ao analisar JSON: %s\n", cJSON_GetErrorPtr());
        }
        else
        {
            cJSON *command = cJSON_GetObjectItem(json, "change-mode");
            if (command != NULL && command->type == cJSON_String && command->valuestring != NULL)
            {
                handle_central_server_command(command->valuestring[0]);
            }
            else
            {
                printf("JSON não contém um campo 'command-mode' válido.\n");
            }

            cJSON_Delete(json);
        }
    }
}

char *create_traffic_info_json()
{
    cJSON *root = cJSON_CreateObject();
    cJSON *main = cJSON_CreateObject();
    cJSON *aux = cJSON_CreateObject();
    cJSON *violations = cJSON_CreateObject();

    cJSON *west_speeds = cJSON_CreateArray();
    cJSON *east_speeds = cJSON_CreateArray();
    cJSON *north_speeds = cJSON_CreateArray();
    cJSON *south_speeds = cJSON_CreateArray();

    cJSON_AddNumberToObject(root, "intersection_id", intersection_id);
    cJSON_AddNumberToObject(root, "execution_time", SEND_CENTRAL_INFO_TIME);

    cJSON_AddNumberToObject(violations, "red_light", trafficViolationsTemp.red_light_violation_count);
    cJSON_AddNumberToObject(violations, "limit_speed", trafficViolationsTemp.exceeded_limit_speed_count);

    for (int i = 0; i < tempSpeeds.main_west_temp_count; i++)
        cJSON_AddItemToArray(west_speeds, cJSON_CreateNumber(tempSpeeds.main_west_speeds[i]));
    for (int i = 0; i < tempSpeeds.main_east_temp_count; i++)
        cJSON_AddItemToArray(east_speeds, cJSON_CreateNumber(tempSpeeds.main_east_speeds[i]));
    for (int i = 0; i < tempSpeeds.aux_south_temp_count; i++)
        cJSON_AddItemToArray(south_speeds, cJSON_CreateNumber(tempSpeeds.aux_south_speeds[i]));
    for (int i = 0; i < tempSpeeds.aux_north_temp_count; i++)
        cJSON_AddItemToArray(north_speeds, cJSON_CreateNumber(tempSpeeds.aux_north_speeds[i]));

    cJSON_AddItemToObject(main, "west_speeds", west_speeds);
    cJSON_AddItemToObject(main, "east_speeds", east_speeds);
    cJSON_AddItemToObject(aux, "north_speeds", north_speeds);
    cJSON_AddItemToObject(aux, "south_speeds", south_speeds);

    cJSON_AddNumberToObject(main, "west_count", 0);
    cJSON_AddNumberToObject(main, "east_count", 0);
    cJSON_AddNumberToObject(aux, "north_count", 0);
    cJSON_AddNumberToObject(aux, "south_count", 0);

    cJSON_AddItemToObject(root, "main", main);
    cJSON_AddItemToObject(root, "aux", aux);
    cJSON_AddItemToObject(root, "violations", violations);

    char *json_str = cJSON_Print(root);

    trafficViolationsTemp.exceeded_limit_speed_count = 0;
    trafficViolationsTemp.red_light_violation_count = 0;
    tempSpeeds.aux_north_temp_count = 0;
    tempSpeeds.aux_south_temp_count = 0;
    tempSpeeds.main_east_temp_count = 0;
    tempSpeeds.main_west_temp_count = 0;
    cJSON_Delete(root);

    return json_str;
}

void handle_send_traffic_info_to_central()
{
    while (1)
    {
        sleep(SEND_CENTRAL_INFO_TIME);
        char *message_str = create_traffic_info_json();
        if (send(sockfd, message_str, strlen(message_str), 0) == -1)
        {
            printf("Trying to connect again!\n");
            config_connection_with_central();
        }
    }
}

char *read_file(const char *filename)
{
    FILE *file = fopen(filename, "r");
    if (file == NULL)
    {
        perror("Error opening the file");
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);

    char *content = (char *)malloc(file_size + 1);
    if (content == NULL)
    {
        perror("Error allocating memory");
        fclose(file);
        return NULL;
    }

    size_t read = fread(content, 1, file_size, file);
    if (read != file_size)
    {
        perror("Error reading the file");
        fclose(file);
        free(content);
        return NULL;
    }

    content[file_size] = '\0';

    fclose(file);

    return content;
}

void config_connection_with_central()
{
    const char *filename = "config/central-addr.json";

    char *fileContent = read_file(filename);

    // Analisar o conteúdo JSON
    cJSON *json = cJSON_Parse(fileContent);
    if (json == NULL)
    {
        perror("Erro ao analisar o JSON");
        free(fileContent);
        return;
    }

    // Obter o endereço IP e a porta do JSON
    cJSON *ipObject = cJSON_GetObjectItem(json, "ip");
    cJSON *portObject = cJSON_GetObjectItem(json, "port");
    if (ipObject == NULL || portObject == NULL)
    {
        perror("O JSON não contém os campos 'ip' e 'port'");
        cJSON_Delete(json);
        free(fileContent);
        return;
    }

    const char *ip = ipObject->valuestring;
    int port = portObject->valueint;

    // Criar um socket TCP
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        perror("Erro ao criar o socket\n");
        return 1;
    }

    // Definir o endereço IP e a porta do servidor
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip);

    // Conectar ao servidor
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("Erro ao conectar ao servidor\n");
        close(sockfd);
        return 1;
    }

    cJSON_Delete(json);
    free(fileContent);
    printf("Connected to server!\n");
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        printf("Uso: %s <numero>\n", argv[0]);
        return 1;
    }

    intersection_id = atoi(argv[1]);
    define_pins(intersection_id);

    if (!bcm2835_init())
        exit(1);

    set_pins();
    signal(SIGINT, handle_interruption);
    init_states();
    config_connection_with_central();

    pthread_t traffic_lights_thread, buttons_thread, sensors_thread, send_info_to_central_thread, recive_central_message_thread;

    pthread_create(&traffic_lights_thread, NULL, handle_all_traffic_lights, NULL);
    pthread_create(&buttons_thread, NULL, handle_all_buttons, NULL);
    pthread_create(&sensors_thread, NULL, handle_all_sensors, NULL);
    pthread_create(&send_info_to_central_thread, NULL, handle_send_traffic_info_to_central, NULL);
    pthread_create(&recive_central_message_thread, NULL, handle_recive_central_message, NULL);

    pthread_join(traffic_lights_thread, NULL);
    pthread_join(buttons_thread, NULL);
    pthread_join(sensors_thread, NULL);
    pthread_join(send_info_to_central_thread, NULL);
    pthread_join(recive_central_message_thread, NULL);
}
