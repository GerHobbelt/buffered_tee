# buffered_tee

Windows/UNIX command line utility which buffers all `stdin` input in memory before processing a la `tee`, `sort`, `uniq`, `stdbuf` UNIX tools.


## Command line

```
./buffered_tee.exe [OPTIONS]

OPTIONS:
  -h,     --help              Print this help message and exit
  -o,     --outfile TEXT ...  specify the file location of the output
  -p,     --progress          show progress as '.' dots instead of echoing lines from stdin
  -a,     --append            append to each output file instead of create/overwrite. 
                              This behaviour is similar to `tee -a`.
  -q,     --quiet             do NOT echo any lines to stdout/stderr nor show any progress dots.
  -u,     --unique            deduplicate the input text lines; implies `--sort`
  -s,     --sort              sort the input text lines in lexicographic order, before writing to output file.
  -c,     --cleanup           filter every input line that's written to stdout to ensure no
                              characters are included that may confuse the console/terminal emulator:
							  any C0 or non-ASCII character is replaced by '.'
  -r,     --redux UINT        reduced stderr progress noise: output 1 line for each N input lines.
```


## `stdout` vs. `stderr`: which do we use for what?

1. text lines are copied from `stdin` to `stdout`.

   The filters applied in this output stream are:

   - `--quiet` which turns this output OFF.
   - `--progress` which turns this output OFF and replaces it with a stream of '.' dots written to `stderr` instead.
   - `--cleanup` which filters the input and replaces any non-printable / non-ASCII character with a simple '.' dot.
   - `--redux N` which drops every N-1 lines, i.e. 1 line output per N.
   
2. all warnings, errors and progress dots are written to `stderr`.

   This is done so you can have progress feedback visible on your console, while the command is still part of a larger chain of `|` piped UNIX-style commands.

   The filters applied in this output stream are:

   - `--quiet` which turns this output OFF.
   - `--progress` which writes a '.' progress dot for every line read/written from/to stdin/stdout/output-files.
   - `--redux N` which drops every N-1 dots, i.e. reduces the output to 1 dot output per N lines processed.
   
   
   
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
  



