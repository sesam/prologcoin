#include "builtins_fileio.hpp"
#include "interpreter.hpp"
#include <boost/filesystem.hpp>
#include <fstream>

namespace prologcoin { namespace interp {

    using namespace prologcoin::common;

    static std::unordered_map<size_t, std::istream> file_map_;

    bool builtins_fileio::open_3(interpreter &interp, term &caller)
    {
        term filename0 = interp.env().arg(caller, 0);
	term mode0 = interp.env().arg(caller, 1);
	term stream = interp.env().arg(caller, 2);

	if (!interp.env().is_atom(filename0)) {
	    interp.abort(interpreter_exception_wrong_arg_type(
		      "open/3: Filename must be an atom; was: "
		      + interp.env().to_string(filename0)));
	}

	if (!interp.env().is_atom(mode0)) {
	    interp.abort(interpreter_exception_wrong_arg_type(
		      "open/3: Mode must be an atom; was: "
		      + interp.env().to_string(mode0)));
	}

	std::string full_path = interp.get_full_path(interp.env().atom_name(filename0));
	std::string mode = interp.env().atom_name(mode0);

	if (!boost::filesystem::exists(full_path)) {
	    interp.abort(interpreter_exception_file_not_found(
		    "open/3: File '" + full_path + "' not found"));
	}

	if (mode != "read" && mode != "write") {
	    interp.abort(interpreter_exception_wrong_arg_type(
		    "open/3: Mode must be 'read' or 'write'; was: " + mode));
	}

	if (mode == "read") {
	    std::ios_base *ios = new std::ifstream(full_path);
	    size_t id = interp.register_file(ios);
	    con_cell f = interp.env().functor("$stream", 1);
	    term newstream = interp.env().new_term(f, {interp.env().to_term(int_cell(id))} );

	    return interp.env().unify(stream, newstream);
	}

	// TODO: Mode write...

	return false;
    }

    bool builtins_fileio::close_1(interpreter &interp, term &caller)
    {
	term stream = interp.env().arg(caller, 0);
	if (!interp.env().is_functor(stream, con_cell("$stream", 1))) {
	    interp.abort(interpreter_exception_wrong_arg_type(
		      "close/1: Expected stream argument; was: "
		      + interp.env().to_string(stream)));
	}
	term stream_id = interp.env().arg(stream, 0);
	if (stream_id->tag() != tag_t::INT) {
	    interp.abort(interpreter_exception_wrong_arg_type(
		      "close/1: Unrecognized stream identifier: "
		      + interp.env().to_string(stream_id)));
	}

	cell sid = *stream_id;
	int_cell &intid = static_cast<int_cell &>(sid);
	size_t id = intid.value();
	if (!interp.is_file_id(id)) {
	    interp.abort(interpreter_exception_file_not_found(
		      "close/1: Identifier is not an open file: " +
		      boost::lexical_cast<std::string>(id)));
	}

	return true;
    }
}}