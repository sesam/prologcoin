#include "builtins.hpp"
#include "interpreter.hpp"
#include "arithmetics.hpp"

namespace prologcoin { namespace interp {

    using namespace prologcoin::common;

    int_cell arithmetics_fn::get_int(const term &t)
    {
	cell c = *t;
	const int_cell &ic = static_cast<const int_cell &>(c);
	return ic;
    }

    term arithmetics_fn::plus_2(interpreter &interp, term *args)
    {
	return interp.env().to_term(get_int(args[0]) + get_int(args[1]));
    }

    term arithmetics_fn::minus_2(interpreter &interp, term *args)
    {
	return interp.env().to_term(get_int(args[0]) - get_int(args[1]));
    }

    term arithmetics_fn::times_2(interpreter &interp, term *args)
    {
	return interp.env().to_term(get_int(args[0]) * get_int(args[1]));
    }

    void arithmetics::load_fn(const std::string &name, size_t arity, arithmetics::fn fn)
    {
	con_cell f = interp_.env().functor(name, arity);
	fn_map_[f] = fn;
    }

    void arithmetics::load_fns()
    {
	if (fn_map_.size() != 0) {
	    return;
	}
        load_fn("+", 2, &arithmetics_fn::plus_2);
        load_fn("-", 2, &arithmetics_fn::minus_2);
        load_fn("*", 2, &arithmetics_fn::times_2);
    }

    void arithmetics::unload()
    {
	fn_map_.clear();
	args_.clear();
    }

    term arithmetics::eval(term &expr,
			   const std::string &context)
    {
	load_fns();

	term result;
	term_env &env = interp_.env();
	size_t stack_start = env.stack_size();
	env.push(expr);
	env.push(env.to_term(int_cell(0)));
	while (stack_start < env.stack_size()) {
	    cell visited0 = *env.pop();
	    term t = env.pop();
	    const int_cell &visited = static_cast<const int_cell &>(visited0);
            if (t->tag() == tag_t::INT) {
		args_.push_back(t);
		continue;
	    }
	    if (visited.value() != 0) {
		cell c = *t;
		const con_cell &f = static_cast<const con_cell &>(c);
		size_t arity = f.arity();
		auto fn_call = lookup(f);
		if (fn_call == nullptr) {
		    interp_.abort(interpreter_exception_undefined_function(
			   context + ": Undefined function: " +
			   env.atom_name(f) + "/" +
			   boost::lexical_cast<std::string>(arity) +
			   " in " + env.safe_to_string(expr)));
		}
		size_t end = args_.size();
		size_t off = end - arity;
		if (is_debug()) {
		    std::cout << "arithmetics::eval(): " << 
    		        env.atom_name(f) + "/" +
			boost::lexical_cast<std::string>(arity) + "(";
		    bool first = true;
		    for (size_t i = off; i < end; i++) {
			if (!first) std::cout << ", ";
			std::cout << env.to_string(args_[i]);
			first = false;
		    }
		    std::cout << ")\n";
		}
		result = fn_call(interp_, &args_[off]);
		args_.resize(off);
		env.push(result);
		env.push(env.to_term(int_cell(1)));
		continue;
	    }
	    switch (t->tag()) {
	    case tag_t::CON:
	    case tag_t::STR: {
		con_cell f = env.functor(t);
		env.push(env.to_term(f));
		env.push(env.to_term(int_cell(1)));
		size_t n = f.arity();
		for (size_t i = 0; i < n; i++) {
		    term arg = env.arg(t, n - i - 1);
		    env.push(arg);
		    env.push(env.to_term(int_cell(0)));
		}
		break;
	    }
	    case tag_t::REF: {
		interp_.abort(interpreter_exception_not_sufficiently_instantiated(context + ": Arguments are not sufficiently instantiated"));
		break;
	    }
	    case tag_t::BIG: {
		interp_.abort(interpreter_exception_unsupported(context + ": Big integers are unsupported."));
		break;
	    }
	    case tag_t::INT: {
		assert(false); // Should not occur
		break;
	    }
	    }
	}
	args_.clear();
	return result;
    }

    //
    // Simple
    //

    int_cell arithmetics::get_int_arg_type(term &arg,
					   const std::string &context)
    {
	if (arg->tag() != tag_t::INT) {
	    interp_.abort(interpreter_exception_argument_not_number(
			      context + ": argument is not a number: " +
			      interp_.env().safe_to_string(arg)));
	}
	cell c = *arg;
	int_cell &r = static_cast<int_cell &>(c);
	return r;
    }

}}