
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <chrono>
#include <algorithm>
#include <future>
#include <atomic>
#include <chrono>
#include <thread>
#include <format>

#ifdef CLI11_SINGLE_FILE
#include "CLI11.hpp"
#else
#include "CLI/CLI.hpp"
#endif
#include "CLI/Timer.hpp"

//#include <ghc/fs_std.hpp>  // namespace fs = std::filesystem;   or   namespace fs = ghc::filesystem;

// run an async timer task which sets a mark every time a bit of progress MAY be shown.
// This takes care of the variable and sometimes obnoxiously high '.' dot progress rate/output.
class ProgressTimer
{
	std::future<int> t;
	std::atomic<bool> ticked = false;
	std::atomic<bool> must_stop = false;

public:
	void init(void)
	{
		t = std::async(std::launch::async, &ProgressTimer::timer_task, this);
	}

	~ProgressTimer()
	{
		must_stop = true;
		t.wait();
		(void)t.get();
	}

	int timer_task(void)
	{
		using namespace std::chrono_literals;

		ticked = true;

		while (!must_stop) {
			ticked = true;

			std::this_thread::sleep_for(125ms);
		}

		ticked = true;

		return 0;
	}

	void show_progress(void)
	{
		auto triggered = ticked.exchange(false);
		if (triggered) {
			std::cerr << ".";
		}
	}
};


// We accept '.' and '-' as file names representing stdin/stdout:
static bool is_stdin_stdout(const std::string &filename) {
	return (filename == "." || filename == "-" || filename == "/dev/stdin" || filename == "/dev/stdout");
}


/*
* Fetch input from stdin until EOF.
*
* Buffer everything and separate the input into text lines, which will be sorted and de-duplicated.
*/

int main(int argc, const char **argv) {
	CLI::App app{"buffered_tee"};
	CLI::Timer timer;
	ProgressTimer progress;

	std::vector<std::string> inFiles;
	app.add_option("--infile,-i", inFiles, "specify the file location of an input file") /* ->required() */;
	std::vector<std::string> outFiles;
	app.add_option("--outfile,-o", outFiles, "specify the file location of an output file") /* ->required() */;
	bool show_progress = false;
	app.add_flag("-p,--progress", show_progress);
	bool append_to_file = false;
	app.add_flag("-a,--append", append_to_file);
	bool quiet_mode = false;
	app.add_flag("-q,--quiet", quiet_mode);
	bool deduplicate = false;
	app.add_flag("-u,--unique", deduplicate, "deduplication implies --sort");
	bool sorting = false;
	app.add_flag("-s,--sort", sorting);
	bool cleanup_stderr = false;
	app.add_flag("-c,--cleanup", cleanup_stderr);
	std::optional<std::uint64_t> redux_opt;
	app.add_option("-r,--redux", redux_opt, "reduced stdout output / stderr progress noise: output 1 line for each N input lines.");

	CLI11_PARSE(app, argc, argv);

	if (outFiles.empty()) {
		std::cerr << "Warning: no output files specified. All you'll see is the progress/echo to stderr/stdout!\n    Use --outfile or -o to specify at least one output file." << std::endl;

		outFiles.push_back("-"); // use stdout as output
	}

	if (inFiles.empty()) {
		std::cerr << "Notice: no input files specified: STDIN is used instead!\n    Use --infile or -i to specify at least one output file." << std::endl;

		inFiles.push_back("-"); // use stdin as input
	}

	if (quiet_mode && redux_opt) {
		std::cerr << "Warning: --redux option is ignored when --quiet is enabled." << std::endl;
	}

	if (deduplicate)
		sorting = true;

	uint64_t redux_lines = redux_opt.value_or(0);

	progress.init();

	// read lines from stdin/inputs:
	// 
	// https://stackoverflow.com/questions/6089231/getting-std-ifstream-to-handle-lf-cr-and-crlf
	std::vector<std::string> lines;


	if (!quiet_mode) {
		if (show_progress) {
			std::cerr << "Reading from input files...";
		}
	}

	double timer_rd_time;
	std::optional<double> timer_sort_time;
	std::optional<double> timer_wr_time;

	CLI::Timer timer_rd;
	{
		//CLI::Timer timer_rd;

		for (const auto &inFile : inFiles) {
			if (is_stdin_stdout(inFile)) {
				// use stdin

				//ifs.emplace_back(std::cin.rdbuf());

				std::string line;
				while (std::getline(std::cin, line)) {
					lines.push_back(line);

					// show progress if requested
					if (!quiet_mode) {
						if (redux_lines <= 1 || lines.size() % redux_lines == 1) {
							if (show_progress) {
								progress.show_progress();
							}
						}
					}
				}
			} else {
				// open file
				std::ifstream ifs(inFile);
				if (!ifs) {
					std::cerr << std::endl << "Error opening input file: " << inFile << std::endl;
					return 1;
				}

				std::string line;
				while (std::getline(ifs, line)) {
					lines.push_back(line);

					// show progress if requested
					if (!quiet_mode) {
						if (redux_lines <= 1 || lines.size() % redux_lines == 1) {
							if (show_progress) {
								progress.show_progress();
							}
						}
					}
				}
			}
		}

#if 0
		// do NOT stop & get the elpased time WITHIN the scope block as that would introduce
		// the COSTLY mistake of NOT INCLUDING THE TIME SPENT in the destructors!!
		timer_rd_time = timer_rd();
#endif
	}
	timer_rd_time = timer_rd();

	// NOTE: right now, all input files have been read and *closed*; their content resides in lines[].

	size_t written_line_count = 0;

	if (lines.empty()) {
		if (!quiet_mode) {
			if (show_progress) {
				std::cerr << std::endl << "Warning: Input feed is empty (no text lines read). We will SKIP writing the output files!" << std::endl;
			}
		}
	}
	else {
		if (!quiet_mode) {
			if (show_progress) {
				std::cerr << std::endl;
			}
		}

		// performance: sort/dedup *references* to the line strings, so that we don't have to copy/swap the strings themselves.
		// https://stackoverflow.com/questions/25108854/initializing-the-size-of-a-c-vector
		// https://stackoverflow.com/questions/33767668/performance-comparison-of-stl-sort-on-vector-of-strings-vs-vector-of-string-pointers

#if 0  // useless; rest of code that follows has been deleted; this chunk is kept as a reminder only.
		std::vector<std::string &> line_refs; // error C2338: static_assert failed: 'The C++ Standard forbids allocators for reference elements because of [allocator.requirements].'
		line_refs.reserve(lines.size());
		for (auto &l : lines) {
			line_refs.push_back(l);
		}
#endif

		CLI::Timer timer_sort;
		if (sorting) {
			// sort and deduplicate lines
			std::sort(lines.begin(), lines.end());

			if (!quiet_mode) {
				if (show_progress) {
					std::cerr << "Sorted." << std::endl;
				}
			}

			if (deduplicate) {
				auto last = std::unique(lines.begin(), lines.end());
				size_t dropped_dupe_count = std::distance(last, lines.end());
				size_t remaining_count = std::distance(lines.begin(), last);
				lines.erase(last, lines.end());

				if (!quiet_mode) {
					if (show_progress) {
						std::cerr << "Deduplicated; dropped " << dropped_dupe_count << " / " << remaining_count << " lines." << std::endl;
					}
				}
			}

			//timer_sort_time = timer_sort();
		}

		if (sorting)
			timer_sort_time = timer_sort();

		// write to output files
		//
		// Note: when any of them fails to open, then abort all output.
		CLI::Timer timer_wr;
		{
			bool stdout_is_one_of_the_outputs = false;
			std::vector<std::ofstream> ofs;
			for (const auto &outFile : outFiles) {
				if (is_stdin_stdout(outFile)) {
					// use stdout
					stdout_is_one_of_the_outputs = true;
				}
				else {
					std::ofstream of(outFile, (append_to_file ? std::ios::app : std::ios::trunc));
					if (!of) {
						std::cerr << "Error opening output file: " << outFile << std::endl;
						return 1;
					}
					ofs.push_back(std::move(of));
				}
			}

			if (!quiet_mode) {
				if (show_progress) {
					std::cerr << "Writing to output files...";
				}
			}

			for (/* const */ std::string& l : lines) {
				for (auto &outFile : ofs) {
					outFile << l << '\n';
				}
				if (stdout_is_one_of_the_outputs) {
					std::cout << l << '\n';
				}
				written_line_count++;

				// show progress if requested
				if (!quiet_mode) {
					if (redux_lines <= 1 || written_line_count % redux_lines == 1) {
						if (show_progress) {
							progress.show_progress();
						}
						else /* if (!stdout_is_one_of_the_outputs) */ {
							if (cleanup_stderr) {
								// replace all non-ASCII, non-printable characters in string with '.':
								std::replace_if(l.begin(), l.end(), [](char ch) {
									return (static_cast<unsigned char>(ch) < 32 || static_cast<unsigned char>(ch) > 126);
								}, '.');
							}
							std::cerr << l << '\n';
						}
					}
				}
			}

			//timer_wr_time = timer_wr();
		} // end of scope for the output files --> auto-close!

		timer_wr_time = timer_wr();

		if (!quiet_mode) {
			if (show_progress) {
				std::cerr << std::endl;
			}
		}
	}

	if (!quiet_mode) {
		std::cerr << "All done." << std::endl;
		std::cerr << written_line_count << " lines written." << std::endl;
		std::cerr << "Timing report:" << std::endl;

		auto cvt = [](double time) -> std::pair<double, std::string> {
			if (time < .000001)
				return {time * 1000000000, "ns"};
			if (time < .001)
				return {time * 1000000, "us"};
			if (time < 1)
				return {time * 1000, "ms"};
			return {time, "s"};
		};

		auto t = cvt(timer_rd_time);
		std::cerr << std::format("{:<25}{:9.3f}{:>3}", "Reading inputs", t.first, t.second) << std::endl;
		if (timer_sort_time.has_value()) {
			t = cvt(timer_sort_time.value());
			std::cerr << std::format("{:<25}{:9.3f}{:>3}", "Sorting the lines", t.first, t.second) << std::endl;
		}
		t = cvt(timer_wr_time.value_or(0.0));
		std::cerr << std::format("{:<25}{:9.3f}{:>3}", "Writing to output", t.first, t.second) << std::endl;
		t = cvt(timer());
		std::cerr << std::format("{:<25}{:9.3f}{:>3}", "Total time taken", t.first, t.second) << std::endl;
	}

	return 0;
}
