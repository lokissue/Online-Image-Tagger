/*
** COMP30023 Project A image-tagger
 * The images are provided by server, player pairs up with another player,
 * inputs keywords of image until a keyword was submitted by other.
 * Date: 27/4/2019
 * @author Luoming Zhang
*/

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/uio.h>

// constants
static char const * const HTTP_200_FORMAT = "HTTP/1.1 200 OK\r\n\
Set-Cookie: id= %d \r\n\
Content-Type: text/html\r\n\
Content-Length: %ld\r\n\r\n";
static char const * const HTTP_400 = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
static int const HTTP_400_LENGTH = 47;
static char const * const HTTP_404 = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
static int const HTTP_404_LENGTH = 45;
static char const * const INSERT_TEXT = "\r\n<p>%s</p>\r\n";

/** Define the max char length */
#define MAX_C 20

/** Define the max # player*/
#define MAX_P 10

/** Define the default buff size */
#define BUFF_SIZE 2048

/** Define the default text size */
#define TEXT_SIZE 300

/** Define a smaller text size */
#define TEXT_SIZE_S 250

/** Represents the types of method */
typedef enum
{
    GET,
    POST,
    UNKNOWN
} METHOD;

/** The structure of user's data stored in server
 *  @param char username[MAX_C] The username
 *  @param char stage[MAX_C] The page(html) that server sent to client previously
 *  @param char keyword[MAX_C][MAX_C] The keyword list stores all keywords input in each turn
 *  @param int int num_keywords The counter of the number of keywords input
 *  @param int int other_index The index indicates index in user_data of
 *  the paired player in each game turn, not paired is -1
 *  @param char image_index The char indicates the image used in each turn
 */
typedef struct {
    char username[MAX_C];
    char stage[MAX_C];
    char keyword[MAX_C][MAX_C];
    int num_keywords;
    int other_index;
    char image_index;
} User_data;

/** Prototypes */
int method_GET(int sockfd,char *buff, char *html_name, User_data *user_data,
               char *image_index);
bool method_POST(int sockfd,char *buff, char *html_name,
                 User_data *user_data, int *index, char *image_index);
int read_cookie(char *buff);
char* extract_message(char *str, char *start, char *end);
void store_keyword(User_data *user_data, int cookie_id, char *keyword);
char* all_keywords(User_data *user_data, int cookie_id);
int pairing(User_data *user_data, int cookie_id, int index);
bool keyword_match(User_data *user_data, int cookie_id, char *keyword);
void initialise_status(User_data *user_data, int cookie_id);
char* image_controller(char *buff, char *image_index);


/**
 * The http request handle function
 * @param sockfd socket file descriptor
 * @return Boolean true for the http request is properly handled
 *                 false otherwise
 */
static bool handle_http_request(int sockfd)
{
    // User database and its initialisation
    static User_data user_data[MAX_P] = {{{'\0'}, {'\0'},{{'\0'}},0,-1, '0'}};
    // the number of users have registered in the server
    static int index = 0;
    static char image_index = '2';
    // try to read the request
    char buff[BUFF_SIZE+1];
    int n = read(sockfd, buff, BUFF_SIZE+1);
    char html_name[MAX_C];

    if (n <= 0)
    {
        if (n < 0)
            perror("read");
        else
            printf("socket %d close the connection\n", sockfd);
        return false;
    }

    // terminate the string
    buff[n] = 0;

    char * curr = buff;
    int cookie_id = read_cookie(buff);

    // parse the method
    METHOD method = UNKNOWN;
    if (strncmp(curr, "GET ", 4) == 0)
    {
        curr += 4;
        method = GET;
    }
    else if (strncmp(curr, "POST ", 5) == 0)
    {
        curr += 5;
        method = POST;
    }
    else if (write(sockfd, HTTP_400, HTTP_400_LENGTH) < 0)
    {
        perror("write");
        return false;
    }
    // sanitise the URI
    while (*curr == '.' || *curr == '/' || *curr == '?')
        ++curr;
    // assume the only valid request URI is "/" but it can be modified to accept more files
    if (*curr == ' '){
        if(method == GET && cookie_id < 0){
            strcpy(html_name, "1_intro.html");
        }else{
            strcpy(html_name, "2_start.html");
        }
    }
    else if(strncmp(curr, "start", 5) == 0){
        // game state
        if (method == GET || !strcmp(user_data[cookie_id].stage, "6_endgame.html")){
            strcpy(html_name, "3_first_turn.html");
            //start a game, try to pair other player, set image index to player
            user_data[cookie_id].image_index = image_index;
            initialise_status(user_data, cookie_id);
            pairing(user_data, cookie_id, index);
        }else{
            //Try to pair other player
            if (pairing(user_data, cookie_id, index)){
                //Successfully pair, input will be accepted
                strcpy(html_name, "4_accepted.html");
            }
            //If other player win/leave, direct player to end game
            else if(!strcmp(user_data[cookie_id].stage, "4_accepted.html") &&
                     user_data[cookie_id].other_index < 0){
                initialise_status(user_data, cookie_id);
                strcpy(html_name, "6_endgame.html");
                method_GET(sockfd, buff, html_name, user_data, &image_index);
            }
            // pairing failed, input discarded
            else if(user_data[cookie_id].other_index < 0){
                strcpy(html_name, "5_discarded.html");
            }
        }
    }
        // send 404
    else if (write(sockfd, HTTP_404, HTTP_404_LENGTH) < 0)
    {
        perror("write");
        return false;
    }
    if (method == GET) {
        method_GET(sockfd, buff, html_name, user_data, &image_index);
    }else if (method == POST)
    {
        method_POST(sockfd, buff, html_name, user_data, &index, &image_index);
    }else {
        // never used, just for completeness
        fprintf(stderr, "no other methods supported");
    }
    return true;
}

/**
 * Get request handle function
 * @param sockfd socket file descriptor
 * @param buff the buff that read http request
 * @param html_name the name of html file is going to send
 * @param user_data all users' data
 * @param image_index  The index of image that server sending
 * @return int 0 for the html file is successfully sent, 1 otherwise
 */
int method_GET(int sockfd,char *buff, char *html_name, User_data *user_data,
               char *image_index){
    // get the size of the file
    struct stat st;
    stat(html_name, &st);
    long size = st.st_size;
    char added_text[TEXT_SIZE];
    int added_text_length = 0;
    int cookie_id = read_cookie(buff);
    if(cookie_id >= 0){
        strcpy(user_data[cookie_id].stage,html_name);
    }
    //update the size of the file
    //username may need to be inserted to start.html
    if(!strcmp(html_name,"2_start.html")){
        sprintf(added_text, INSERT_TEXT, user_data[cookie_id].username);
        added_text_length = strlen(added_text);
        size = size + added_text_length;
    }

    int n = sprintf(buff, HTTP_200_FORMAT, cookie_id, size);
    // send the header first
    if (write(sockfd, buff, n) < 0) {
        perror("write");
        return 1;
    }
    // read html file to the buffer
    int filefd = open(html_name, O_RDONLY);
    n = read(filefd, buff, BUFF_SIZE);
    if (n < 0) {
        perror("read");
        close(filefd);
        return 1;
    }
    close(filefd);
    //dynamically edit html file and insert username
    if(!strcmp(html_name,"2_start.html")){
        char *p1 = strstr(buff, "<form method=");
        size_t len = strlen(buff) - strlen(p1);
        char new_buff[BUFF_SIZE + added_text_length];
        strncpy(new_buff, buff, len);
        new_buff[len] = '\0';
        strcat(new_buff,added_text);
        strcat(new_buff,p1);
        if (write(sockfd, new_buff, size) < 0)
        {
            perror("write");
            return false;
        }
        return 0;
    }
    if(!strcmp(html_name,"3_first_turn.html") ||
       !strcmp(html_name,"5_discarded.html")){
        image_controller(buff, image_index);
    }
    if (write(sockfd, buff, size) < 0) {
        perror("write");
        return 1;
    }
    return 0;
}

/**
 * Post request handle function
 * @param sockfd socket file descriptor
 * @param buff the buff that read http request
 * @param html_name the name of html file is going to send
 * @param user_data all users' data
 * @param index the number of users registered in the server
 * @param image_index The index of image that server sending
 * @return Boolean true for the html file is successfully sent, false otherwise
 */
bool method_POST(int sockfd,char *buff, char *html_name,
                 User_data *user_data, int *index, char *image_index){
    char *post_message = NULL;
    char *p1;
    int cookie_id = read_cookie(buff);
    int n = 0;
    //"user=" is an indicator of creating a new user
    if(strstr(buff,"user=")){
        p1 = strstr(buff,"user=") + 5;
        post_message = p1;
        // add a new user and initialise data;
        strcpy(user_data[*index].username, post_message);
        cookie_id = *index;
        user_data[cookie_id].num_keywords = 0;
        user_data[cookie_id].other_index = -1;
        user_data[cookie_id].image_index = *image_index;
        initialise_status(user_data, cookie_id);
        *index = *index + 1;
    }
    // player inputs keyword
    else if (strstr(buff,"keyword=")){
        post_message = extract_message(buff, "keyword=","&guess=");
        //keyword was submitted by other previously
        if (keyword_match(user_data, cookie_id, post_message)){
            //switch image index on the server
            if(*image_index == '2'){
                *image_index = '1';
            }else{
                *image_index = '2';
            }
            initialise_status(user_data, cookie_id);
            method_GET(sockfd, buff, "6_endgame.html", user_data, image_index);
            return true;
        }else if(!strcmp(html_name,"5_discarded.html")){
            method_GET(sockfd, buff, "5_discarded.html", user_data, image_index);
            return true;
        }
        //put keyword into list
        store_keyword(user_data,cookie_id, post_message);
        //sting that contains all keyword input by a player
        post_message = all_keywords(user_data, cookie_id);
    }
    // qui game, send game over page and exit
    else if(strstr(buff, "quit=")){
        initialise_status(user_data, cookie_id);
        method_GET(sockfd, buff, "7_gameover.html", user_data, image_index);
        return true;
    }
    //update player stage
    strcpy(user_data[cookie_id].stage, html_name);
    char added_text[TEXT_SIZE];
    sprintf(added_text, INSERT_TEXT, post_message);
    int added_text_length = strlen(added_text);
    // get the size of the file
    struct stat st;
    stat(html_name, &st);
    // increase file size to accommodate the username
    long size = st.st_size + added_text_length;
    n = sprintf(buff, HTTP_200_FORMAT, cookie_id, size);
    // send the header first
    if (write(sockfd, buff, n) < 0)
    {
        perror("write");
        return false;
    }
    // read the content of the HTML file
    int filefd = open(html_name, O_RDONLY);
    n = read(filefd, buff, BUFF_SIZE);
    if (n < 0)
    {
        perror("read");
        close(filefd);
        return false;
    }
    close(filefd);
    //dynamically edit html and insert text
    char *buff_split = strstr(buff, "<form method=");
    size_t len = strlen(buff) - strlen(buff_split);
    char new_buff[BUFF_SIZE + added_text_length];
    strncpy(new_buff, buff, len);
    new_buff[len] = '\0';

    strcat(new_buff,added_text);
    strcat(new_buff,buff_split);
    //control which image is shown
    if(!strcmp(html_name,"4_accepted.html")){
        image_controller(new_buff, image_index);
    }
    if (write(sockfd, new_buff, size) < 0)
    {
        perror("write");
        return false;
    }
    return true;
}

/**
 * Read cookie function
 * @param buff the buff that read http request
 * @return int index of user_data list that
 *             store a particular user's data (also called ID)
 */
int read_cookie(char *buff){
    char *index;
    //if the http request doesn't contain Cookie, return -1
    if(strstr(buff, "Cookie: ") == NULL){ return -1; }
    index = extract_message(buff,"id=", "\r\n");
    return atoi(index);
}

/**
 * Extract message(substring) between two positions
 * @param str a string contains all message
 * @param start position 1, start from
 * @param end   position 2, end.
 * @return  a substring between position 1 and 2.
 */
char* extract_message(char *str, char *start, char *end){
    char *p1 = strstr(str, start) + strlen(start);
    char *p2 = strstr(p1, end);
    long len = p2 - p1;
    char *substr = (char *) malloc(len + 1);
    if (p1 == NULL || p2 == NULL){
        substr[0] = '\0';
        return substr;
    }
    strncpy(substr, p1, len);
    substr[len] = '\0';
    return substr;
}

/**
 * Store keyword into particular user's keyword list
 * @param user_data all users' data
 * @param cookie_id ID of a particular user's data
 * @param keyword   keyword input by player
 */
void store_keyword(User_data *user_data, int cookie_id, char *keyword){
    int n = user_data[cookie_id].num_keywords;
    strcpy(user_data[cookie_id].keyword[n], keyword);
    user_data[cookie_id].num_keywords++;
    return;
}

/**
 * a toString function of keyword list
 * @param user_data all users' data
 * @param cookie_id ID of a particular user's data
 * @return  char* a string of all keywords input by a player in each turn
 */
char* all_keywords(User_data *user_data, int cookie_id){
    int n = user_data[cookie_id].num_keywords;
    if(n <= 1){
        return user_data[cookie_id].keyword[0];
    }else{
        char* str = malloc(TEXT_SIZE_S);
        strcpy(str, user_data[cookie_id].keyword[0]);
        for (int i = 1; i < n ; i++ ){
            strcat(str,", ");
            strcat(str,user_data[cookie_id].keyword[i]);
        }
        return str;
    }
}

/**
 * Pairing two player
 * @param user_data all users' data
 * @param cookie_id ID of a particular user's data
 * @param index the number of users registered in the server
 * @return 1 for a player has been successfully paired, 0 otherwise
 */
int pairing(User_data *user_data, int cookie_id, int index){
    //all un-paired players are initialise as -1
    // exit if a player is already paired
    if(user_data[cookie_id].other_index != -1){ return 1; }
    //loop all other players
    for (int i = 0; i < index; i++){
        // pair condition: not itself and other player is un-paired
        if(i != cookie_id && user_data[i].other_index == -1){
            //pair condition: same image index (same image shown in the game)
            if(user_data[i].image_index == user_data[cookie_id].image_index) {
                //pair condition: player is currently at first_turn page
                //or discarded page
                if (!(strcmp(user_data[i].stage, "3_first_turn.html")
                      && strcmp(user_data[i].stage, "5_discarded.html"))) {
                    user_data[cookie_id].other_index = i;
                    user_data[i].other_index = cookie_id;
                    return 1;
                }
            }
        }
    }
    return 0;
}

/**
 * check a freshly input keyword if submitted by paired player
 * @param user_data all users' data
 * @param cookie_id ID of a particular user's data
 * @param keyword keyword input by player
 * @return Boolean true if keyword found, false otherwise
 */
bool keyword_match(User_data *user_data, int cookie_id, char *keyword){
    int other_index = user_data[cookie_id].other_index;
    //exit if self is un-paired
    if (other_index < 0){
        return false;
    }else{
        int n = user_data[other_index].num_keywords;
        for (int i = 0; i < n; i++){
            if(!strcmp(keyword, user_data[other_index].keyword[i])){
                return true;
            }
        }
    }
    return false;
}

/**
 * Initialise pairing status and number of keywords
 * @param user_data all users' data
 * @param cookie_id ID of a particular user's data
 */
void initialise_status(User_data *user_data, int cookie_id){
    int other = user_data[cookie_id].other_index;
    // initialise player status
    user_data[cookie_id].other_index = -1;
    user_data[cookie_id].num_keywords = 0;
    // initialise paired player status, if self was paired before
    if(other >= 0){
        user_data[other].other_index = -1;
        user_data[other].num_keywords = 0;
    }
    return;
}

/**
 * Handle which image should be sent by server
 * @param buff the buffer that read the html file is about to send
 * @param image_index The index of image that server sending
 * @return a modifier buffer that store html file
 */
char* image_controller(char *buff, char *image_index){
    //char image_index = user_data[cookie_id].image_index;
    char *p1 = strstr(buff,"image-") + strlen("image-");
    if( p1 != NULL){
        *(p1 + 0) = *image_index;
        return buff;
    }
    return buff;
}


int main(int argc, char * argv[])
{

    if (argc < 3)
    {
        fprintf(stderr, "usage: %s ip port\n", argv[0]);
        return 0;
    }

    // create TCP socket which only accept IPv4
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // reuse the socket if possible
    int const reuse = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) < 0)
    {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    // create and initialise address we will listen on
    struct sockaddr_in serv_addr;
    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    // if ip parameter is not specified
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));

    // bind address to socket
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    // listen on the socket
    listen(sockfd, 5);

    // initialise an active file descriptors set
    fd_set masterfds;
    FD_ZERO(&masterfds);
    FD_SET(sockfd, &masterfds);
    // record the maximum socket number
    int maxfd = sockfd;

    while (1)
    {
        // monitor file descriptors
        fd_set readfds = masterfds;
        if (select(FD_SETSIZE, &readfds, NULL, NULL, NULL) < 0)
        {
            perror("select");
            exit(EXIT_FAILURE);
        }

        // loop all possible descriptor
        for (int i = 0; i <= maxfd; ++i)
            // determine if the current file descriptor is active
            if (FD_ISSET(i, &readfds))
            {
                // create new socket if there is new incoming connection request
                if (i == sockfd)
                {
                    struct sockaddr_in cliaddr;
                    socklen_t clilen = sizeof(cliaddr);
                    int newsockfd = accept(sockfd, (struct sockaddr *)&cliaddr, &clilen);
                    if (newsockfd < 0)
                        perror("accept");
                    else
                    {
                        // add the socket to the set
                        FD_SET(newsockfd, &masterfds);
                        // update the maximum tracker
                        if (newsockfd > maxfd)
                            maxfd = newsockfd;
                        // print out the IP and the socket number
                        char ip[INET_ADDRSTRLEN];
                        printf(
                                "new connection from %s on socket %d\n",
                                // convert to human readable string
                                inet_ntop(cliaddr.sin_family, &cliaddr.sin_addr, ip, INET_ADDRSTRLEN),
                                newsockfd
                        );
                    }
                }
                    // a request is sent from the client
                else if (!handle_http_request(i))
                {
                    close(i);
                    FD_CLR(i, &masterfds);
                }
            }
    }

    return 0;
}
