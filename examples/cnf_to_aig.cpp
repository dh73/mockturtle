#include <mockturtle/io/dimacs_reader.hpp>
#include <mockturtle/networks/aig.hpp>
#include <mockturtle/io/write_verilog.hpp>
#include <cstring>
#include <fstream>
#include <mockturtle/io/write_aiger.hpp>
#include <mockturtle/algorithms/functional_reduction.hpp>
#include <mockturtle/algorithms/cleanup.hpp>

using namespace mockturtle;
using namespace std;

int main (int argc, char* argv[]) {
	if (argc < 3) {
        	std::cout << "Error: not enough arguments provided" << std::endl;
        	return 1;
    	}
	std::string dimacs = argv[1];

	aig_network aig;
	dimacs_reader reader( aig );
	// Diagnostics
	lorina::text_diagnostics td;
	lorina::diagnostic_engine diag( &td );
	auto result = lorina::read_dimacs( dimacs, reader, &diag );

	if(result != lorina::return_code::success) {
		std::cout << "Error::Read CNF failed.\n";
		return -1;
	}
	std::cout << "Reading DIMACS file step executed correctly." << '\n';
	
	ostringstream out;
	mockturtle::write_aiger( aig, out );
	std::ofstream outfile(argv[2]);
	outfile << out.str();
	outfile.close();
	std::cout << "Writing VERILOG file step executed correctly." << '\n';

	return 0;
}
