struct pipe_pair
{
    char *input_pipe;
    char *output_pipe;
};

struct fd_pair
{
    int input_fd;
    int output_fd;
};

struct server_config
{
    char *mine_file;
    struct pipe_pair *pipes;
    int num_miners;
};
