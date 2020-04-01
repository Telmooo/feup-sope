#ifndef LOG_H_INCLUDED
#define LOG_H_INCLUDED

#define DEFAULT_MODE 0644 /**< @brief Permissions associated with the file */

/**
 * @brief           Init the log
 * @return          File descriptor uppon sucess or 1 otherwise
 */
int init_log();

/**
  * @brief           Set the descriptor of log file in child processes
  * @int             file descriptor
  */
int set_log_descriptor(int descriptor);

//int set_time(struct timeval it);

/**
  * @brief
  * @return             Return the elapsed time since program started
  */
long double elapsed_time();

/**
 * @brief               Write an action to log
 * @param log_action    Description of type of event
 * @param log_info      Additional information about any action
 * @return              0 uppon sucess or 1 otherwhise
 */
int write_log(char *log_action, char *log_info);

/**
 * @brief               Write an action to log
 * @param log_action    Description of type of event
 * @param info          int array to write in log
 * @param size          size of array info
 * @return              0 uppon sucess or 1 otherwhise
 */
int write_log_array(char *log_action, int *info, int size);

/**
 * @brief               Write an action to log
 * @param log_action    Description of type of event
 * @param info          long int to write in log
 * @return              0 uppon sucess or 1 otherwhise
 */
int write_log_int(char *log_action, long log_info);

/**
 * @brief           Close log file
 * @return          0 uppon sucess or 1 otherwhise
 */
int close_log();

#endif // LOG_H_INCLUDED
