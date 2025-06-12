# buffered_tee

Windows/UNIX command line utility which buffers all `stdin` input in memory before processing a la `tee`, `sort`, `uniq`, `stdbuf` UNIX tools.

Input files are read completely before any output files are opened/created and written to. This rule also applies to `stdin` and `stdout` when these are part of the input/output file set.


## Command line

```
./buffered_tee.exe [OPTIONS]

OPTIONS:
  -h,     --help              Print this help message and exit
  -i,     --infile FILE ...   specify one or more file paths for the input;
                              '.' and '-' are special values for stdin.
							  If no input files are specified, stdin is used.
  -o,     --outfile FILE ...  specify one or more file paths for the output;
                              '.' and '-' are special values for stdout.
							  If no output files are specified, stdout is used.
  -p,     --progress          show progress as '.' dots instead of echoing lines
                              from stdin.
  -a,     --append            append to each output file instead of create/overwrite.
                              This behaviour is similar to `tee -a`.
  -q,     --quiet             do NOT echo any lines to stdout/stderr nor show
                              any progress dots.
  -u,     --unique            deduplicate the input text lines; implies `--sort`
  -s,     --sort              sort the input text lines in lexicographic order,
                              before writing to output file.
  -c,     --cleanup           filter every input line that's written to stderr
                              as part of the progress output to ensure no
                              characters are included that may confuse the
							  console/terminal emulator:
							  any C0 or non-ASCII character is replaced by '.'
  -r,     --redux UINT        reduced stderr progress noise:
                              output 1 line for each N input lines.
```


## `stdout` vs. `stderr`: which do we use for what?

1. text lines are copied from `stdin` + input files to `stdout` + output files.

   These "file paths" are recognized as stdin/stdout:

   - '.'
   - '-'

   plus the special file paths:

   - '/dev/stdin'
   - '/dev/stdout'

   Every line read from the inputs is collected and written to each of the output files.
   Every line is followed by a newline character, i.e. the output is always complete text lines.
   
2. all warnings, errors and progress lines/dots are written to `stderr`.

   This is done so you can have progress feedback visible on your console, even while the command is still part of a larger chain of `|` piped UNIX-style commands or when this command's `stdout` is redirected to file by the calling shell script/command.

   The filters applied in this output stream are:

   - `--quiet` which turns this output OFF.
   - `--progress` which writes a '.' progress dot for every line read/written from/to stdin/stdout/output-files.
	 Whithout `--progress` the lines read from `stdin` are echoed to `stderr` as well.
   - `--redux N` which drops every N-1 dots/lines, i.e. reduces the output to 1 dot/line output per N lines processed.
   
   
   
## Why was this tool conceived?

This was written as a *potential quick fix* to be used in a set of `bash` shell utility scripts which we use regularly on Windows / UNIX platforms.

Two problems were looking for a solution there:

- 'rewriting' index/word set files without having to resort to temporary intermediary files, a la:

  ```
  cat $1.dirlist.txt | sort | uniq > $1.dirlist.tmp
  mv  $1.dirlist.tmp  $1.dirlist.txt
  ```
  
  We had previously attempted to solve this using `stdbuf` (no luck) and then using GNU specific:
  
  ```
  sort -S 12G -T /nonexistant/dir -u -o $1.dirlist.txt $1.dirlist.txt
  ```
  
  but somehow this causes spurious deadlocks in the Windows `bash` environment at least.

- we are processing several quite large text files (which, however, would fit in RAM memory entirely on the 128GB dev box!) and were hitting
  HDD I/O as a bottleneck, mostly due to the behaviour of such UNIX piped command chains when both input and output file sit on the same HDD:
  the "streamed" I/O results in interleaved read and write ops on the disk, which result in a significantly raised number of head seek ops
  as the OS/filesystem copes with this load. By buffering the output in RAM in its entirety we now have a nicely serialized sequence of 
  all reads from file X, then all writes to file Y, thus cutting down on the number of head seek back & forth movements for these 1GB+ files.
  
- note that `buffered_tee` also may serve as a `cat` replacement where every input file is guranteed to have been read completely
  before the first line is written to `stdout`, thus making more "file rewriting" statements possible without the need of using intermediate
  temporary files:

  ```
  buffered_cat -i this_particular_file | foo | bar > this_particular_file
  ```

  which is a command that would have surely led to disaster if `cat` were used instead, as it would have started writing to the file
  before all its content might have been read.

  This is useful when you want to process the input text lines in a single pass, e.g. with `sort`, `tr`, `uniq`, etc., without having to resort to temporary files.


