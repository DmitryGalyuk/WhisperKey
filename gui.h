int gui_run(int pipe_read_fd);
void gui_set_pipe(int fd);
void gui_show(const char *emoji);
void gui_hide();
void gui_paste(const char *text);