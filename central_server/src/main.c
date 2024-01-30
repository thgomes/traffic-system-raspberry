#include <stdio.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include "cJSON.h"

#define MAX_CLIENT_CHAR_NAME 50
#define MAX_ROOM_CHAR_NAME 50
#define STDIN 0
#define BUFFER_SIZE 1024
#define MAX_DISTRIBUTED 2

#define ANSI_RESET "\x1B[0m"
#define ANSI_BOLD "\x1B[1m"
#define ANSI_RED "\x1B[31m"
#define ANSI_GREEN "\x1B[32m"
#define ANSI_BLUE "\x1B[34m"
#define ANSI_BG_BLACK "\x1B[40m"
#define ANSI_FG_WHITE "\x1B[37m"

#define EMERGENCY_MODE 'E'
#define NIGHT_MODE 'N'
#define DEFAULT_MODE 'D'

#define MIN_SPEED 3.6

#define MAX_CLIENT 100
typedef struct
{
    int intersection_id;
    char name[MAX_CLIENT_CHAR_NAME];
    struct sockaddr_in addr;
    int client_sockfd;
} Client;

typedef struct
{
    struct sockaddr_in addr;
} Server;

Client clients[MAX_CLIENT];
int sockfd, max_fd, newsockfd, bytes_received, opt = 1;
int clients_count = 0;
fd_set master_fds;
char buffer[BUFFER_SIZE];

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

int create_socket(const char *ip, int port)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
    {
        perror("Erro ao abrir o socket\n");
        exit(1);
    }

    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int)) < 0)
    {
        perror("Erro ao definir as opções do socket\n");
        exit(1);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip); // AQUI
    server_addr.sin_port = htons(port);
    memset(&(server_addr.sin_zero), '\0', 8);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Erro ao fazer o bind\n");
        exit(1);
    }

    if (listen(sockfd, 10) < 0)
    {
        perror("Erro ao ouvir a porta\n");
        exit(1);
    }

    printf("\n");
    printf("Socket %d criado com sucesso\n", sockfd);

    return sockfd;
}

void config_central_server_socket()
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

    sockfd = create_socket(ip, port);

    cJSON_Delete(json);
    free(fileContent);
}

void send_message(int sockfd, const char *message)
{
    send(sockfd, message, strlen(message), 0);
}

void handle_new_connection()
{
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int newsockfd = accept(sockfd, (struct sockaddr *)&client_addr, &client_len);

    if (newsockfd < 0)
    {
        perror("Erro ao aceitar a conexão\n");
        exit(1);
    }

    clients_count++;
    clients[clients_count - 1].addr = client_addr;
    clients[clients_count - 1].client_sockfd = newsockfd;
    snprintf(clients[clients_count - 1].name, sizeof(clients[clients_count - 1].name), "Cliente %d", clients_count);
    printf("\n");
    printf("Client: IP %s, Port %d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
    printf(">>>: ");
    fflush(stdout);

    FD_SET(newsockfd, &master_fds);

    if (newsockfd > max_fd)
    {
        max_fd = newsockfd;
    }
}

void handle_traffic_mode_command(char command)
{
    for (int i = 0; i < MAX_DISTRIBUTED; i++)
    {
        Client client = clients[i];
        cJSON *message = cJSON_CreateObject();

        // Converte o caractere 'command' em uma string
        char command_str[2];
        command_str[0] = command;
        command_str[1] = '\0';

        cJSON_AddStringToObject(message, "change-mode", command_str);
        char *message_str = cJSON_PrintUnformatted(message);
        send_message(client.client_sockfd, message_str);
    }
    printf("Successfully executed!\n");
}

void sum_violations(const char *filename, int *total_speed_limit, int *total_red_light)
{
    char *fileContent = read_file(filename);

    if (fileContent == NULL)
    {
        printf("File not found: %s\n", filename);
        return;
    }

    cJSON *root = cJSON_Parse(fileContent);

    if (root == NULL)
    {
        printf("Error parsing JSON in file: %s\n", filename);
        free(fileContent);
        return;
    }

    cJSON *violations = cJSON_GetObjectItem(root, "violations");

    if (violations == NULL || violations->type != cJSON_Object)
    {
        printf("Invalid violations data in file: %s\n", filename);
        cJSON_Delete(root);
        free(fileContent);
        return;
    }

    cJSON *speed_limit_violation = cJSON_GetObjectItem(violations, "limit_speed");
    cJSON *red_light_violation = cJSON_GetObjectItem(violations, "red_light");

    if (speed_limit_violation == NULL || red_light_violation == NULL ||
        speed_limit_violation->type != cJSON_Number || red_light_violation->type != cJSON_Number)
    {
        printf("Invalid violation data in file: %s\n", filename);
        cJSON_Delete(root);
        free(fileContent);
        return;
    }

    *total_speed_limit += speed_limit_violation->valueint;
    *total_red_light += red_light_violation->valueint;

    cJSON_Delete(root);
    free(fileContent);
}

void print_violations()
{
    int total_speed_limit = 0;
    int total_red_light = 0;

    for (int i = 1; i <= MAX_DISTRIBUTED; i++)
    {
        char filename[100];
        sprintf(filename, "data/traffic-intersection-%d.json", i);
        sum_violations(filename, &total_speed_limit, &total_red_light);
    }

    printf(" ___________________________________________________________ \n");
    printf("                                                             \n");
    printf("  Total Speed limit exceeded violation:                  %d  \n", total_speed_limit);
    printf("  Total Red traffic light violation:                     %d  \n", total_red_light);
    printf(" ___________________________________________________________ \n");
}

void sum_valid_speeds(cJSON *root, double *sum, int *count)
{
    if (root == NULL || sum == NULL || count == NULL)
    {
        fprintf(stderr, "Argumentos inválidos.\n");
        return;
    }

    *sum = 0.0;
    *count = 0;
    if (root->type == cJSON_Array)
    {
        cJSON *item = root->child;

        while (item != NULL)
        {
            if (item->type == cJSON_Number)
            {
                if (item->valuedouble > MIN_SPEED)
                {
                    *sum += item->valuedouble;
                    *count += 1;
                }
            }
            item = item->next;
        }
    }
    else
    {
        fprintf(stderr, "O JSON não contém um objeto válido.\n");
    }
}

void calculate_lanes_averange_speed(const char *filename, int intersection_id, double *total_main_speed, double *total_main_count, double *first_aux_avg, double *second_aux_avg)
{
    char *fileContent = read_file(filename);

    if (fileContent == NULL)
    {
        printf("File not found: %s\n", filename);
        return;
    }

    cJSON *root = cJSON_Parse(fileContent);

    if (root == NULL)
    {
        printf("Error parsing JSON in file: %s\n", filename);
        free(fileContent);
        return;
    }

    cJSON *aux = cJSON_GetObjectItem(root, "aux");
    cJSON *main = cJSON_GetObjectItem(root, "main");

    if (aux == NULL || aux->type != cJSON_Object || main == NULL || main->type != cJSON_Object)
    {
        printf("Invalid aux or main data in file: %s\n", filename);
        cJSON_Delete(root);
        free(fileContent);
        return;
    }

    cJSON *aux_north_speeds = cJSON_GetObjectItem(aux, "north_speeds");
    cJSON *aux_south_speeds = cJSON_GetObjectItem(aux, "south_speeds");

    cJSON *main_east_speeds = cJSON_GetObjectItem(main, "east_speeds");
    cJSON *main_west_speeds = cJSON_GetObjectItem(main, "west_speeds");

    if (aux_north_speeds == NULL || aux_south_speeds == NULL || main_west_speeds == NULL || main_east_speeds == NULL ||
        aux_north_speeds->type != cJSON_Array || aux_south_speeds->type != cJSON_Array ||
        main_west_speeds->type != cJSON_Array || main_east_speeds->type != cJSON_Array)
    {
        printf("Invalid speed data in file: %s\n", filename);
        cJSON_Delete(root);
        free(fileContent);
        return;
    }

    double aux_north_speed_sum, aux_south_speed_sum, main_west_speed_sum, main_east_speed_sum;
    int aux_north_speed_count, aux_south_speed_count, main_west_speed_count, main_east_speed_count;

    sum_valid_speeds(aux_north_speeds, &aux_north_speed_sum, &aux_north_speed_count);
    sum_valid_speeds(aux_south_speeds, &aux_south_speed_sum, &aux_south_speed_count);
    sum_valid_speeds(main_east_speeds, &main_east_speed_sum, &main_east_speed_count);
    sum_valid_speeds(main_west_speeds, &main_west_speed_sum, &main_west_speed_count);

    *total_main_count += main_east_speed_count + main_west_speed_count;
    *total_main_speed += main_east_speed_sum + main_west_speed_sum;

    double aux_speeds_sum = aux_north_speed_sum + aux_south_speed_sum;
    double aux_cars_count = aux_south_speed_count + aux_north_speed_count;

    if (intersection_id == 1)
        *first_aux_avg = aux_speeds_sum / aux_cars_count;
    else if (intersection_id == 2)
        *second_aux_avg = aux_speeds_sum / aux_cars_count;

    cJSON_Delete(root);
    free(fileContent);
}

void print_averange_speed()
{
    double total_main_speed = 0;
    double total_main_count = 0;
    double main_avg_speed = 0;
    double first_aux_avg = 0;
    double second_aux_avg = 0;
    for (int i = 1; i <= MAX_DISTRIBUTED; i++)
    {
        char filename[100];
        sprintf(filename, "data/traffic-intersection-%d.json", i);
        calculate_lanes_averange_speed(filename, i, &total_main_speed, &total_main_count, &first_aux_avg, &second_aux_avg);
    }
    main_avg_speed = total_main_speed / total_main_count;
    printf(" ___________________________________________________________ \n");
    printf("                                                             \n");
    printf("  Main traffic lane averange speed:             %.2f km/h    \n", main_avg_speed);
    printf("  First auxiliar traffic lane averange speed:   %.2f km/h    \n", first_aux_avg);
    printf("  Second auxiliar traffic lane averange speed:  %.2f km/h    \n", second_aux_avg);
    printf(" ___________________________________________________________ \n");
}

void calculate_traffic_flow(const char *filename, int intersection_id, double *first_aux_flow, double *second_aux_flow, double *total_main_time, double *total_main_count)
{
    char *fileContent = read_file(filename);

    if (fileContent == NULL)
    {
        printf("File not found: %s\n", filename);
        return;
    }

    cJSON *root = cJSON_Parse(fileContent);

    if (root == NULL)
    {
        printf("Error parsing JSON in file: %s\n", filename);
        free(fileContent);
        return;
    }

    cJSON *main = cJSON_GetObjectItem(root, "main");
    cJSON *aux = cJSON_GetObjectItem(root, "aux");

    if (aux == NULL || aux->type != cJSON_Object)
    {
        printf("Invalid aux data in file: %s\n", filename);
        cJSON_Delete(root);
        free(fileContent);
        return;
    }

    cJSON *execution_time = cJSON_GetObjectItem(root, "execution_time");
    cJSON *aux_north_count = cJSON_GetObjectItem(aux, "north_count");
    cJSON *aux_south_count = cJSON_GetObjectItem(aux, "south_count");
    cJSON *main_east_count = cJSON_GetObjectItem(main, "east_count");
    cJSON *main_west_count = cJSON_GetObjectItem(main, "west_count");

    if (execution_time == NULL || aux_north_count == NULL || aux_south_count == NULL ||
        main_east_count == NULL || main_west_count == NULL || execution_time->type != cJSON_Number ||
        aux_north_count->type != cJSON_Number || aux_south_count->type != cJSON_Number ||
        main_east_count->type != cJSON_Number || main_west_count->type != cJSON_Number)
    {
        printf("Invalid speed data in file: %s\n", filename);
        cJSON_Delete(root);
        free(fileContent);
        return;
    }

    *total_main_count += main_east_count->valueint + main_west_count->valueint;
    *total_main_time += execution_time->valuedouble;

    if (intersection_id == 1)
        *first_aux_flow = (aux_north_count->valueint + aux_north_count->valueint) / execution_time->valuedouble;
    else if (intersection_id == 2)
        *second_aux_flow = (aux_north_count->valueint + aux_north_count->valueint) / execution_time->valuedouble;

    cJSON_Delete(root);
    free(fileContent);
}

void print_traffic_flow()
{
    double total_main_time = 0;
    double total_main_count = 0;
    double main_flow = 0;
    double first_aux_flow = 0;
    double second_aux_flow = 0;
    for (int i = 1; i <= MAX_DISTRIBUTED; i++)
    {
        char filename[100];
        sprintf(filename, "data/traffic-intersection-%d.json", i);
        calculate_traffic_flow(filename, i, &first_aux_flow, &second_aux_flow, &total_main_time, &total_main_count);
    }

    main_flow = total_main_count / total_main_time;
    printf(" ___________________________________________________________  \n");
    printf("                                                          \n");
    printf("  Main lane traffic flow:              %.2lf cars/min     \n", main_flow * 60);
    printf("  First lane auxiliar traffic flow:    %.2lf cars/min     \n", first_aux_flow * 60);
    printf("  Second lane auxiliar traffic flow:   %.2lf cars/min     \n", second_aux_flow * 60);
    printf(" ___________________________________________________________  \n");
}

void handle_stdin_input()
{
    int intersection_id;
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));
    if (read(STDIN, buffer, sizeof(buffer)) <= 0)
    {
        perror("Erro na leitura da entrada padrão\n");
        exit(1);
    }
    else if (strncmp(buffer, "emergency mode", strlen("emergency mode")) == 0)
        handle_traffic_mode_command(EMERGENCY_MODE);
    else if (strncmp(buffer, "night mode", strlen("night mode")) == 0)
        handle_traffic_mode_command(NIGHT_MODE);
    else if (strncmp(buffer, "default mode", strlen("default mode")) == 0)
        handle_traffic_mode_command(DEFAULT_MODE);
    else if (strncmp(buffer, "show violations", strlen("show violations")) == 0)
        print_violations();
    else if (strncmp(buffer, "show commands", strlen("show commands")) == 0)
        print_commands_list();
    else if (strncmp(buffer, "show avgspeed", strlen("show avgspeed")) == 0)
        print_averange_speed();
    else if (strncmp(buffer, "show trafficflow", strlen("show trafficflow")) == 0)
        print_traffic_flow();
    else
        printf("Invalid command, type 'show commands'\n");

    printf(">>>: ");
    fflush(stdout);
}

void get_speeds_from_json(cJSON *root, cJSON **main_west_speeds, cJSON **main_east_speeds, cJSON **aux_north_speeds, cJSON **aux_south_speeds)
{
    cJSON *aux = cJSON_GetObjectItem(root, "aux");
    cJSON *main = cJSON_GetObjectItem(root, "main");
    if (aux == NULL || aux->type != cJSON_Object || main == NULL || main->type != cJSON_Object)
    {
        printf("Invalid aux or main data in existent file\n");
        cJSON_Delete(root);
        free(root);
        return;
    }
    *aux_north_speeds = cJSON_GetObjectItem(aux, "north_speeds");
    *aux_south_speeds = cJSON_GetObjectItem(aux, "south_speeds");
    *main_west_speeds = cJSON_GetObjectItem(main, "west_speeds");
    *main_east_speeds = cJSON_GetObjectItem(main, "east_speeds");
    if (*aux_north_speeds == NULL || *aux_south_speeds == NULL || *main_west_speeds == NULL || *main_east_speeds == NULL ||
        (*aux_north_speeds)->type != cJSON_Array || (*aux_south_speeds)->type != cJSON_Array ||
        (*main_west_speeds)->type != cJSON_Array || (*main_east_speeds)->type != cJSON_Array)
    {
        printf("Invalid speeds data in file.\n");
        cJSON_Delete(root);
        free(root);
        return;
    }
}

void get_cars_count_from_json(cJSON *root, cJSON **main_west_count, cJSON **main_east_count, cJSON **aux_north_count, cJSON **aux_south_count)
{
    cJSON *aux = cJSON_GetObjectItem(root, "aux");
    cJSON *main = cJSON_GetObjectItem(root, "main");
    if (aux == NULL || aux->type != cJSON_Object || main == NULL || main->type != cJSON_Object)
    {
        printf("Invalid aux or main data in existent file\n");
        cJSON_Delete(root);
        free(root);
        return;
    }
    *aux_north_count = cJSON_GetObjectItem(aux, "north_count");
    *aux_south_count = cJSON_GetObjectItem(aux, "south_count");
    *main_west_count = cJSON_GetObjectItem(main, "west_count");
    *main_east_count = cJSON_GetObjectItem(main, "east_count");
    if (*aux_north_count == NULL || *aux_south_count == NULL || *main_west_count == NULL || *main_east_count == NULL ||
        (*aux_north_count)->type != cJSON_Number || (*aux_south_count)->type != cJSON_Number ||
        (*main_west_count)->type != cJSON_Number || (*main_east_count)->type != cJSON_Number)
    {
        printf("Invalid count data in file.\n");
        cJSON_Delete(root);
        free(root);
        return;
    }
}

void merge_new_informations_to_data(cJSON *mainjson, cJSON *auxjson)
{
    cJSON *mainjson_execution_time = cJSON_GetObjectItem(mainjson, "execution_time");
    cJSON *auxjson_execution_time = cJSON_GetObjectItem(auxjson, "execution_time");
    if (mainjson_execution_time && mainjson_execution_time->type == cJSON_Number &&
        auxjson_execution_time && auxjson_execution_time->type == cJSON_Number)
        mainjson_execution_time->valuedouble += auxjson_execution_time->valuedouble;
    else
        printf("Error: not valid execution time field");

    cJSON *mainjson_aux_north_count, *mainjson_aux_south_count, *mainjson_main_east_count, *mainjson_main_west_count;
    get_cars_count_from_json(mainjson, &mainjson_main_west_count, &mainjson_main_east_count, &mainjson_aux_north_count, &mainjson_aux_south_count);

    cJSON *mainjson_aux_north_speeds, *mainjson_aux_south_speeds, *mainjson_main_west_speeds, *mainjson_main_east_speeds;
    cJSON *auxjson_aux_north_speeds, *auxjson_aux_south_speeds, *auxjson_main_west_speeds, *auxjson_main_east_speeds;
    get_speeds_from_json(mainjson, &mainjson_main_west_speeds, &mainjson_main_east_speeds, &mainjson_aux_north_speeds, &mainjson_aux_south_speeds);
    get_speeds_from_json(auxjson, &auxjson_main_west_speeds, &auxjson_main_east_speeds, &auxjson_aux_north_speeds, &auxjson_aux_south_speeds);

    for (int i = 0; i < cJSON_GetArraySize(auxjson_aux_north_speeds); i++)
    {
        cJSON *speed = cJSON_GetArrayItem(auxjson_aux_north_speeds, i);
        cJSON_AddItemToArray(mainjson_aux_north_speeds, cJSON_Duplicate(speed, 1));
        mainjson_aux_north_count->valuedouble = cJSON_GetArraySize(mainjson_aux_north_speeds);
    }
    for (int i = 0; i < cJSON_GetArraySize(auxjson_aux_south_speeds); i++)
    {
        cJSON *speed = cJSON_GetArrayItem(auxjson_aux_south_speeds, i);
        cJSON_AddItemToArray(mainjson_aux_south_speeds, cJSON_Duplicate(speed, 1));
        mainjson_aux_south_count->valuedouble = cJSON_GetArraySize(mainjson_aux_south_speeds);
    }
    for (int i = 0; i < cJSON_GetArraySize(auxjson_main_east_speeds); i++)
    {
        cJSON *speed = cJSON_GetArrayItem(auxjson_main_east_speeds, i);
        cJSON_AddItemToArray(mainjson_main_east_speeds, cJSON_Duplicate(speed, 1));
        mainjson_main_east_count->valuedouble = cJSON_GetArraySize(mainjson_main_east_speeds);
    }
    for (int i = 0; i < cJSON_GetArraySize(auxjson_main_west_speeds); i++)
    {
        cJSON *speed = cJSON_GetArrayItem(auxjson_main_west_speeds, i);
        cJSON_AddItemToArray(mainjson_main_west_speeds, cJSON_Duplicate(speed, 1));
        mainjson_main_west_count->valuedouble = cJSON_GetArraySize(mainjson_main_west_speeds);
    }

    cJSON *mainjson_violations = cJSON_GetObjectItem(mainjson, "violations");
    cJSON *auxjson_violations = cJSON_GetObjectItem(auxjson, "violations");

    if (mainjson_violations == NULL || auxjson_violations == NULL ||
        mainjson_violations->type != cJSON_Object || auxjson_violations->type != cJSON_Object)
    {
        printf("Invalid violation object\n");
        return;
    }

    cJSON *mainjson_red_light_violations = cJSON_GetObjectItem(mainjson_violations, "red_light");
    cJSON *mainjson_speed_limit_violations = cJSON_GetObjectItem(mainjson_violations, "limit_speed");
    cJSON *auxjson_red_light_violations = cJSON_GetObjectItem(auxjson_violations, "red_light");
    cJSON *auxjson_speed_limit_violations = cJSON_GetObjectItem(auxjson_violations, "limit_speed");

    if (mainjson_red_light_violations == NULL || mainjson_speed_limit_violations == NULL ||
        auxjson_red_light_violations == NULL || auxjson_speed_limit_violations == NULL ||
        mainjson_red_light_violations->type != cJSON_Number || mainjson_speed_limit_violations->type != cJSON_Number ||
        auxjson_red_light_violations->type != cJSON_Number || auxjson_speed_limit_violations->type != cJSON_Number)

    {
        printf("Invalid type to specific violation\n");
        return;
    }

    mainjson_red_light_violations->valuedouble += auxjson_red_light_violations->valuedouble;
    mainjson_speed_limit_violations->valuedouble += auxjson_speed_limit_violations->valuedouble;
}

void save_traffic_statistics(const char *buffer)
{
    cJSON *root = cJSON_Parse(buffer); // Analisa o JSON

    if (root == NULL)
    {
        printf("Erro ao analisar o JSON.\n");
        return;
    }

    // Get the value of the "intersection_id" field from the JSON
    cJSON *intersection_id = cJSON_GetObjectItem(root, "intersection_id");
    cJSON *execution_time = cJSON_GetObjectItem(root, "execution_time");
    if (intersection_id == NULL || intersection_id->type != cJSON_Number ||
        execution_time == NULL || execution_time->type != cJSON_Number)
    {
        printf("Not valid number fields for intersection_id or/and execution_time.\n");
        cJSON_Delete(root);
        return;
    }

    char file_name[100];
    snprintf(file_name, sizeof(file_name), "data/traffic-intersection-%d.json", intersection_id->valueint);

    // Remova o objeto "intersection" do JSON
    cJSON_DeleteItemFromObject(root, "intersection_id");

    // Get existent speed informations
    char *existent_file_content = read_file(file_name);
    cJSON *existent_json = cJSON_Parse(existent_file_content);
    if (existent_json == NULL)
    {
        printf("Erro ao analisar o JSON.\n");
        return;
    }
    merge_new_informations_to_data(root, existent_json);

    // Abre arquivo para escrita
    FILE *file = fopen(file_name, "w");

    if (file == NULL)
    {
        printf("Erro ao abrir o arquivo para escrita.\n");
        cJSON_Delete(root);
        return;
    }

    // Escreve o conteúdo JSON modificado no arquivo
    char *formated_json = cJSON_PrintUnformatted(root);
    fprintf(file, "%s", formated_json);

    fclose(file);
    cJSON_Delete(root); // Libera a memória alocada pela cJSON
    free(formated_json);
}

void remove_client(int client_sockfd)
{
    int num_clients = MAX_CLIENT;
    for (int i = 0; i < num_clients; i++)
    {
        if (clients[i].client_sockfd == client_sockfd)
        {
            clients[i] = clients[num_clients - 1];
            num_clients--;
            return;
        }
    }
}

void handle_client_message(int client_sockfd)
{
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));
    int bytes_received = recv(client_sockfd, buffer, sizeof(buffer), 0);

    if (bytes_received > 0)
    {
        save_traffic_statistics(buffer);
        return;
    }
    else if (bytes_received == 0)
    {
        printf("Conexão fechada pelo cliente.\n");
        FD_CLR(client_sockfd, &master_fds);
        remove_client(client_sockfd);
    }
    else
    {
        perror("Erro ao receber dados");
        FD_CLR(client_sockfd, &master_fds);
        remove_client(client_sockfd);
    }
}

void print_commands_list()
{
    printf(" ___________________________________________________________\n");
    printf("                                                           \n");
    printf("  show commands           -   List all commands            \n");
    printf("  emergency mode          -   Turn on emergency mode       \n");
    printf("  night mode              -   Turn on night mode           \n");
    printf("  default mode            -   Turn on default mode         \n");
    printf("  show violations         -   Show all traffic violations  \n");
    printf("  show avgspeed           -   Show averange speed          \n");
    printf("  show trafficflow        -   Show traffic flow            \n");
    printf(" ___________________________________________________________\n");
}

int main(int argc, char **argv)
{
    config_central_server_socket();

    FD_ZERO(&master_fds);
    FD_SET(sockfd, &master_fds);
    FD_SET(STDIN, &master_fds);

    max_fd = sockfd;

    print_commands_list();
    printf(">>>: ");
    fflush(stdout);

    while (1)
    {
        fd_set read_fds = master_fds;
        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0)
        {
            perror("Erro no select\n");
            exit(1);
        }

        for (int fd = 0; fd <= max_fd; fd++)
        {
            if (FD_ISSET(fd, &read_fds))
            {
                if (fd == sockfd)
                {
                    handle_new_connection();
                }
                else if (fd == STDIN)
                    handle_stdin_input();
                else
                    handle_client_message(fd);
            }
        }
    }

    return 0;
}
