#include "../common/term_env.hpp"
#include "interpreter_base.hpp"
#include "builtins_fileio.hpp"
#include "builtins_opt.hpp"
#include <boost/filesystem.hpp>
#include <boost/timer/timer.hpp>

#define PROFILER 0

namespace prologcoin { namespace interp {

using namespace prologcoin::common;

const common::term code_point::fail_term_ = common::ref_cell(0);

interpreter_base::interpreter_base() : register_pr_("", 0), comma_(",",2), empty_list_("[]", 0), implied_by_(":-", 2), arith_(*this)
{
    init();

    load_builtins();
    load_builtins_opt();
}

void interpreter_base::init()
{
    debug_ = false;
    file_id_count_ = 1;
    num_of_args_= 0;
    memset(register_ai_, 0, sizeof(register_ai_));
    stack_ = reinterpret_cast<word_t *>(new char[MAX_STACK_SIZE]);
    stack_ptr_ = 0;
    num_y_fn_ = &num_y;
    id_to_predicate_.push_back(predicate()); // Reserve index 0
    prepare_execution();
}

interpreter_base::~interpreter_base()
{
    arith_.unload();
    close_all_files();
    register_cp_.reset();
    register_qr_ = term();
    query_vars_.clear();
    syntax_check_stack_.clear();
    builtins_.clear();
    builtins_opt_.clear();
    program_db_.clear();
    program_predicates_.clear();
    predicate_id_.clear();
    id_to_predicate_.clear();
}

void interpreter_base::close_all_files()
{
    for (auto f : open_files_) {
	delete f.second;
    }
    open_files_.clear();
}

bool interpreter_base::is_file_id(size_t id) const
{
    return open_files_.find(id) != open_files_.end();
}

file_stream & interpreter_base::new_file_stream(const std::string &path)
{
    size_t new_id = file_id_count_;
    file_stream *fs = new file_stream(*this, file_id_count_, path);
    file_id_count_++;
    open_files_[new_id] = fs;
    return *fs;
}

void interpreter_base::close_file_stream(size_t id)
{
    if (open_files_.find(id) == open_files_.end()) {
        return;
    }
    file_stream *fs = open_files_[id];
    open_files_.erase(id);
    delete fs;
}

file_stream & interpreter_base::get_file_stream(size_t id)
{
    return *open_files_[id];
}

void interpreter_base::syntax_check()
{
    while (!syntax_check_stack_.empty()) {
	auto f = syntax_check_stack_.back();
	syntax_check_stack_.pop_back();
	f();
    }
}

void interpreter_base::load_clause(const term t)
{
    syntax_check_stack_.push_back(
		  std::bind(&interpreter_base::syntax_check_clause, this,
			    t));
    syntax_check();

    // This is a valid clause. Let's lookup the functor of its head.

    term head = clause_head(t);

    con_cell predicate = functor(head);
    
    auto found = program_db_.find(predicate);
    if (found == program_db_.end()) {
        program_db_[predicate] = std::vector<term>();
	program_predicates_.push_back(predicate);
    }
    program_db_[predicate].push_back(t);
}

void interpreter_base::load_builtin(con_cell f, builtin b)
{
    auto found = builtins_.find(f);
    if (found == builtins_.end()) {
        builtins_[f] = b;
    }
}

void interpreter_base::load_builtin_opt(con_cell f, builtin_opt b)
{
    auto found = builtins_opt_.find(f);
    if (found == builtins_opt_.end()) {
        builtins_opt_[f] = b;
    }    
}

void interpreter_base::load_builtins()
{
    // Profiling
    load_builtin(con_cell("profile", 0), &builtins::profile_0);

    // Simple
    load_builtin(con_cell("true",0), &builtins::true_0);

    // Control flow
    load_builtin(con_cell(",",2), &builtins::operator_comma);
    load_builtin(con_cell("!",0), &builtins::operator_cut);
    load_builtin(con_cell("_!",0), &builtins::operator_cut_if);
    load_builtin(con_cell(";",2), &builtins::operator_disjunction);
    load_builtin(con_cell("->",2), &builtins::operator_if_then);

    // Standard order, equality and unification

    load_builtin(con_cell("@<",2), &builtins::operator_at_less_than);
    load_builtin(con_cell("@=<",2), &builtins::operator_at_equals_less_than);
    load_builtin(con_cell("@>",2), &builtins::operator_at_greater_than);
    load_builtin(con_cell("@>=",2), &builtins::operator_at_greater_than_equals);
    load_builtin(con_cell("==",2), &builtins::operator_equals);
    load_builtin(con_cell("\\==",2), &builtins::operator_not_equals);
    load_builtin(con_cell("compare",3), &builtins::compare_3);
    load_builtin(con_cell("=",2), &builtins::operator_unification);
    load_builtin(con_cell("\\=",2), &builtins::operator_cannot_unify);

    // Type tests
    load_builtin(con_cell("var",1), &builtins::var_1);
    load_builtin(con_cell("nonvar",1), &builtins::nonvar_1);
    load_builtin(con_cell("integer",1), &builtins::integer_1);
    load_builtin(con_cell("number",1), &builtins::number_1);
    load_builtin(con_cell("atom",1), &builtins::atom_1);
    load_builtin(con_cell("atomic",1), &builtins::atomic_1);
    load_builtin(functor("compound",1), &builtins::compound_1);
    load_builtin(functor("callable",1), &builtins::callable_1);
    load_builtin(con_cell("ground", 1), &builtins::ground_1);

    // Arithmetics
    load_builtin(con_cell("is",2), &builtins::is_2);

    // Analyzing & constructing terms
    load_builtin(functor("functor",3), &builtins::functor_3);
    load_builtin(functor("copy_term",2), &builtins::copy_term_2);
    load_builtin(con_cell("=..", 2), &builtins::operator_deconstruct);

    // Meta
    load_builtin(con_cell("\\+", 1), &builtins::operator_disprove);
}

void interpreter_base::load_builtins_opt()
{
    // Profiling
    load_builtin_opt(con_cell("member", 2), &builtins_opt::member_2);
    load_builtin_opt(con_cell("sort", 2), &builtins_opt::sort_2);
}

void interpreter_base::enable_file_io()
{
    load_builtins_file_io();
}

void interpreter_base::set_current_directory(const std::string &dir)
{
    current_dir_ = dir;
}

std::string interpreter_base::get_full_path(const std::string &path) const
{
    boost::filesystem::path p(current_dir_);
    p /= path;
    return p.string();
}

void interpreter_base::load_builtins_file_io()
{
    load_builtin(con_cell("open", 3), &builtins_fileio::open_3);
    load_builtin(con_cell("close", 1), &builtins_fileio::close_1);
    load_builtin(con_cell("read", 2), &builtins_fileio::read_2);
    load_builtin(functor("at_end_of_stream", 1), &builtins_fileio::at_end_of_stream_1);
    load_builtin(con_cell("write", 1), &builtins_fileio::write_1);
    load_builtin(con_cell("nl", 0), &builtins_fileio::nl_0);
}

void interpreter_base::load_program(const term t)
{
    syntax_check_stack_.push_back(
		  std::bind(&interpreter_base::syntax_check_program, this,
			    t));
    syntax_check();

    for (auto clause : list_iterator(*this, t)) {
	load_clause(clause);
    }
}

void interpreter_base::load_program(const std::string &str)
{
    std::stringstream ss(str);
    load_program(ss);
}

void interpreter_base::load_program(std::istream &in)
{
    auto prog = parse(in);
    return load_program(prog);
}

const std::vector<term> & interpreter_base::get_predicate(common::con_cell pn)
{
    return program_db_[pn];
}

void interpreter_base::syntax_check_program(const term t)
{
    if (!is_list(t)) {
	throw syntax_exception_program_not_a_list(t);
    }

    size_t sz = syntax_check_stack_.size();
    for (auto clause : list_iterator(*this, t)) {
	syntax_check_stack_.push_back(
		  std::bind(&interpreter_base::syntax_check_clause, this,
			    clause));
    }
    std::reverse(syntax_check_stack_.begin() + sz, syntax_check_stack_.end());
}

void interpreter_base::syntax_check_clause(const term t)
{
    static const con_cell def(":-", 2);

    auto f = functor(t);
    if (f == def) {
	auto head = arg(t, 0);
	auto body = arg(t, 1);
	syntax_check_stack_.push_back(
	      std::bind(&interpreter_base::syntax_check_head, this, head) );
	syntax_check_stack_.push_back(
	      std::bind(&interpreter_base::syntax_check_body, this, body) );
	return;
    }

    // This is a head only clause.

    syntax_check_stack_.push_back(
		  std::bind(&interpreter_base::syntax_check_head, this, t));
}

void interpreter_base::syntax_check_head(const term t)
{
    static con_cell def(":-", 2);
    static con_cell semi(";", 2);
    static con_cell comma(",", 2);
    static con_cell cannot_prove("\\+", 1);

    if (!is_functor(t)) {
	throw syntax_exception_clause_bad_head(t, "Head of clause is not a functor");
    }

    // Head cannot be functor ->, ; , or \+
    auto f = functor(t);

    if (f == def || f == semi || f == comma || f == cannot_prove) {
	throw syntax_exception_clause_bad_head(t, "Clause has an invalid head; cannot be '->', ';', ',' or '\\+'");
    }
}

void interpreter_base::syntax_check_body(const term t)
{
    static con_cell imply("->", 2);
    static con_cell semi(";", 2);
    static con_cell comma(",", 2);
    static con_cell cannot_prove("\\+", 1);

    if (is_functor(t)) {
	auto f = functor(t);
	if (f == imply || f == semi || f == comma || f == cannot_prove) {
	    auto num_args = f.arity();
	    for (size_t i = 0; i < num_args; i++) {
		auto a = arg(t, i);
		syntax_check_stack_.push_back(
		      std::bind(&interpreter_base::syntax_check_body, this, a));
	    }
	    return;
	}
    }

    syntax_check_stack_.push_back(
		  std::bind(&interpreter_base::syntax_check_goal, this, t));
}

void interpreter_base::syntax_check_goal(const term t)
{
    // Each goal must be a functor (e.g. a plain integer is not allowed)

    if (!is_functor(t)) {
	auto tg = t.tag();
	// We don't know what variables will be bound to, so we need
	// to conservatively skip the syntax check.
	if (tg == tag_t::REF) {
	    return;
	}
	throw syntax_exception_bad_goal(t, "Goal is not callable.");
    }
}

void interpreter_base::print_db() const
{
    print_db(std::cout);
}

void interpreter_base::print_db(std::ostream &out) const
{
    bool do_nl_p = false;
    for (const con_cell p : program_predicates_) {
	auto it = program_db_.find(p);
	if (it == program_db_.end()) {
	    continue;
	}
	const predicate &clauses = it->second;
	if (do_nl_p) {
	    out << "\n";
	}
	bool do_nl = false;
	for (auto clause : clauses) {
	    if (do_nl) out << "\n";
	    auto str = to_string(clause, term_emitter::STYLE_PROGRAM);
	    out << str;
	    do_nl = true;
	}
	do_nl_p = true;
    }
    out << "\n";
}

void interpreter_base::print_profile() const
{
    print_profile(std::cout);
}

void interpreter_base::print_profile(std::ostream &out) const
{
    struct entry {
	con_cell f;
	uint64_t t;

	bool operator < (const entry &e) const {
	    if (t < e.t) {
		return true;
	    } else if (t > e.t) {
		return false;
	    } else {
		return f.value() < e.f.value();
	    }
	}

	bool operator == (const entry &e) const {
	    return t == e.t && f == e.f;
	}
    };

    std::vector<entry> all;

    for (auto prof : profiling_) {
	auto f = prof.first;
	auto t = prof.second;
	all.push_back(entry{f,t});
    }
    std::sort(all.begin(), all.end());
    for (auto p : all) {
	auto f = p.f;
	auto t = p.t;
	std::cout << to_string(f) << ": " << t << "\n";
    }

}

void interpreter_base::abort(const interpreter_exception &ex)
{
    throw ex;
}

void interpreter_base::prepare_execution()
{
    stack_ptr_ = 0;
    num_of_args_= 0;
    memset(register_ai_, 0, sizeof(register_ai_));
    top_fail_ = false;
    register_b_ = nullptr;
    register_e_ = nullptr;
    register_e_is_extended_ = false;
    set_register_hb(get_register_hb());
    register_b0_ = nullptr;
    register_top_b_ = nullptr;
    register_top_e_ = nullptr;
}

bool interpreter_base::execute(const term query)
{
    top_fail_ = false;

    trim_trail(0);

    prepare_execution();

    query_vars_.clear();

    std::unordered_set<std::string> seen;

    // Record all vars for this query
    std::for_each( begin(query),
		   end(query),
		   [&](const term &t) {
		       if (t.tag() == tag_t::REF) {
			   const std::string name = to_string(t);
			   if (!seen.count(name)) {
			       query_vars_.push_back(binding(name,t));
			       seen.insert(name);
			   }
		       }
		   } );

    register_cp_ = code_point(query);
    register_qr_ = query;

    return cont();
}

bool interpreter_base::cont()
{
    do {
	do {
	    execute_once();
	} while (register_e_ != register_top_e_ && !top_fail_);
	
        if (!meta_.empty()) {
	    meta_entry &e = meta_.back();
	    meta_context *mc = e.first;
	    meta_fn fn = e.second;
	    fn(*this, mc);
	    if (top_fail_) {
	        top_fail_ = false;
	        fail();
	    }
        }
	
    } while (register_e_ != 0 && !top_fail_);

    return !top_fail_;
}

bool interpreter_base::next()
{
    fail();
    if (!top_fail_) {
	cont();
    }
    return !top_fail_;
}

std::string interpreter_base::get_result(bool newlines) const
{
    std::unordered_map<term, size_t> count_occurrences;
    std::for_each(begin(register_qr_),
		  end(register_qr_),
		  [&] (const term t) {
		    if (t.tag() == tag_t::REF) {
		      ++count_occurrences[t];
		    }
		  }
		  );

    interpreter_base &ii = const_cast<interpreter_base &>(*this);

    // Those vars with a singleton occurrence will be named
    // '_'.
    size_t named_var_count = 0;
    for (auto v : count_occurrences) {
        if (has_name(v.first)) {
    	    continue;
	}
        if (v.second == 1) {
            ii.set_name(v.first, "_");
	} else { // v.second > 1
  	    std::string name = "G_" + boost::lexical_cast<std::string>(
		       named_var_count);
	    named_var_count++;
	    ii.set_name(v.first, name);
	}
    }

    std::vector<std::string> result;

    bool first = true;

    std::stringstream ss;
    for (auto v : query_vars_) {
	auto &name = v.name();
	auto &value = v.value();
	auto value_str = to_string(value);
	if (name != value_str) {
	    if (!first) {
		if (newlines) {
		    ss << "," << std::endl;
		} else {
		    ss << ", ";
		}
	    }
	    ss << name <<  " = " << value_str;
	    first = false;
	}
    }

    for (auto v : count_occurrences) {
        ii.clear_name(v.first);
    }

    if (first) {
	ss << "true";
    }

    if (newlines) {
	ss << std::endl;
    }

    return ss.str();
}

void interpreter_base::print_result(std::ostream &out) const
{
    out << get_result();
}

void interpreter_base::execute_once()
{
    auto instruction = register_cp_;

    register_cp_ = code_point(empty_list());
    dispatch(instruction);
}

void interpreter_base::tidy_trail()
{
    size_t from = (b() == nullptr) ? 0 : b()->tr;
    size_t to = trail_size();
    term_env::tidy_trail(from, to);
}

bool interpreter_base::definitely_inequal(const term a, const term b)
{
    using namespace common;
    if (a.tag() == tag_t::REF || b.tag() == common::tag_t::REF) {
	return false;
    }
    if (a.tag() != b.tag()) {
	return true;
    }
    switch (a.tag()) {
    case tag_t::REF: return false;
    case tag_t::CON: return a != b;
    case tag_t::STR: {
	con_cell fa = functor(a);
	con_cell fb = functor(b);
        return fa != fb;
    }
    case tag_t::INT: return a != b;
    case tag_t::BIG: return false;
    }

    return false;
}

common::cell interpreter_base::first_arg_index(const term t)
{
    switch (t.tag()) {
    case tag_t::REF: return t;
    case tag_t::CON: return t;
    case tag_t::STR: {
	con_cell f = functor(t);
	return f;
    }
    case tag_t::INT: return t;
    case tag_t::BIG: return t;
    }
    return t;
}

term interpreter_base::clause_head(const term clause)
{
    auto f = functor(clause);
    if (f == implied_by_) {
	return arg(clause, 0);
    } else {
	return clause;
    }
}

term interpreter_base::clause_body(const term clause)
{
    auto f = functor(clause);
    if (f == implied_by_) {
        return arg(clause, 1);
    } else {
        return empty_list();
    }
}

void interpreter_base::compute_matched_predicate(con_cell func,
						 const term first_arg,
						 predicate &matched)
{
    auto &clauses = program_db_[func];
    for (auto &clause : clauses) {
	// Extract head
	auto head = clause_head(clause);
	auto head_functor = functor(head);
	if (head_functor.arity() > 0) {
	    auto head_first_arg = arg(head, 0);
	    if (definitely_inequal(head_first_arg, first_arg)) {
		continue;
	    }
	}
	matched.push_back(clause);
    }
}

size_t interpreter_base::matched_predicate_id(con_cell func, const term first_arg)
{
    term index_arg = first_arg;
    switch (first_arg.tag()) {
    case tag_t::STR:
	index_arg = functor(first_arg); break;
    case tag_t::CON:
    case tag_t::INT:
	break;
    case tag_t::REF:
	index_arg = term();
	break;
    }

    functor_index findex(func, index_arg);
    auto it = predicate_id_.find(findex);
    size_t id;
    if (it == predicate_id_.end()) {
	id = id_to_predicate_.size();
	id_to_predicate_.push_back( predicate() );
	predicate_id_[findex] = id;
	auto &pred = id_to_predicate_[id];
	compute_matched_predicate(func, first_arg, pred);
    } else {
	id = it->second;
    }
    return id;
}

common::term interpreter_base::get_first_arg()
{
    if (num_of_args_ == 0) {
        return empty_list();
    }
    return a(0);
}

void interpreter_base::dispatch(code_point instruction)
{
    register_qr_ = instruction.term_code();

    con_cell f = functor(register_qr_);

    if (f == empty_list_) {
        // Return
        if (is_debug()) {
	    std::cout << "interpreter_base::dispatch(): exit " << to_string(ee()->qr) << "\n";
        }
        deallocate_environment();
	return;
    }

    if (is_debug()) {
        // Print call
        std::cout << "interpreter_base::dispatch(): call " << to_string(instruction.term_code()) << "\n";
    }

#if PROFILER
    struct scope_exit {
	scope_exit(interpreter_base &interp, con_cell f) : interp_(interp), f_(f) { }
	~scope_exit() {
	    auto dt = cpu_.elapsed().system;
	    interp_.profiling_[f_] += dt;
	}
	interpreter_base &interp_;
	con_cell f_;
	boost::timer::cpu_timer cpu_;
    } sc(*this, f);
#endif

    size_t arity = f.arity();
    for (size_t i = 0; i < arity; i++) {
        a(i) = arg(instruction.term_code(), i);
    }
    set_num_of_args(arity);

    // Is this a built-in?
    auto bf = builtins_[f];
    if (bf != nullptr) {
	if (!bf(*this, arity, args())) {
	    fail();
	}
	return;
    }

    // Is there a successful optimized built-in?
    auto obf = builtins_opt_[f];
    if (obf != nullptr) {
        tribool r = obf(*this, arity, args());
	if (!indeterminate(r)) {
	    if (!r) {
		fail();
	    }
	    return;
	}
    }

    auto first_arg = get_first_arg();

    size_t predicate_id = matched_predicate_id(f, first_arg);
    predicate  &p = get_predicate(predicate_id);

    register_pr_ = f;

    // Otherwise a vector of clauses
    auto &clauses = p;

    if (clauses.empty()) {
        clauses = program_db_[f];
	if (clauses.empty()) {
	    std::stringstream msg;
	    msg << "Undefined predicate " << atom_name(f) << "/" << f.arity();
	    abort(interpreter_exception_undefined_predicate(msg.str()));
	    return;
	}

	fail();
	return;
    }

    size_t num_clauses = clauses.size();

    bool has_choices = num_clauses > 1;
    size_t index_id = predicate_id;

    // More than one clause that matches? We need a choice point.
    if (has_choices) {
        // Before making the actual call we'll remember the current choice
        // point. This is the one we backtrack to if we encounter a cut operatio
        set_b0(b());

	int_cell index_id_int(index_id);
	code_point cp(index_id_int);
	allocate_choice_point(cp);
    }

    if (!select_clause(instruction, index_id, clauses, 0)) {
	fail();
    }
}

bool interpreter_base::select_clause(const code_point &instruction,
				     size_t index_id,
				     std::vector<term> &clauses,
				     size_t from_clause)
{
    if (index_id == 0) {
        if (from_clause > 1) {
	    return false;
	}
        set_cp(code_point(arg(register_qr_, from_clause)));
	b()->bp = code_point(int_cell(from_clause+1));
	return true;
    }

    size_t num_clauses = clauses.size();
    bool has_choices = num_clauses > 1;

    // Let's go through clause by clause and see if we can find a match.
    for (size_t i = from_clause; i < num_clauses; i++) {
        auto &clause = clauses[i];

	size_t current_heap = heap_size();
	auto copy_clause = copy(clause); // Instantiate it

	term copy_head = clause_head(copy_clause);
	term copy_body = clause_body(copy_clause);

	if (unify(copy_head, instruction.term_code())) { // Heads match?
	    // Update choice point (where to continue on fail...)
	    if (has_choices) {
	        auto choice_point = b();
		if (i == num_clauses - 1) {
	  	    choice_point->bp = code_point(int_cell(0));
		} else {
		    choice_point->bp = code_point(int_cell((index_id << 8) + (i+1)));
		}
	    }

	    allocate_environment(true);

	    register_cp_ = copy_body;
	    register_qr_ = copy_head;

	    return true;
	} else {
    	    // Discard garbage created on heap (due to copying clause)
	    trim_heap(current_heap);
	}
    }

    // None found.
    return false;
}

void interpreter_base::unwind_to_top_choice_point()
{
    if (top_b() == nullptr) {
        return;
    }
    reset_to_choice_point(top_b());
    set_b(top_b());
}

choice_point_t * interpreter_base::reset_to_choice_point(choice_point_t *b)
{
    auto ch = b;

    set_e(ch->ce);
    set_cp(ch->cp);
    unwind(ch->tr);
    trim_heap(ch->h);
    set_b0(ch->b0);
    set_register_hb(heap_size());
    
    register_qr_ = ch->qr;
    register_pr_ = ch->pr;

    return ch;
}

void interpreter_base::unwind(size_t from_tr)
{
    // Unbind variables
    unwind_trail(from_tr, trail_size());
    trim_trail(from_tr);
}

void interpreter_base::fail()
{
    bool ok = false;

    size_t current_tr = trail_size();
    bool unbound = false;

    do {
	if (is_debug()) {
	    std::cout << "interpreter_base::fail(): fail " << to_string(register_qr_) << "\n";
	}

        if (register_b_ == register_top_b_) {
	    top_fail_ = true;
	    return;
        }

	auto ch = reset_to_choice_point(b());

	size_t bpval = static_cast<const int_cell &>(ch->bp.term_code()).value();

	// Is there another clause to backtrack to?
	if (bpval != 0) {
	    size_t index_id = bpval >> 8;

	    // Unbind variables
	    unwind(current_tr);
	    current_tr = trail_size();

	    unbound = true;

	    if (is_debug()) {
	        std::string redo_str = to_string(register_qr_);
		std::cout << "interpreter_base::fail(): redo " << redo_str << std::endl;
	    }
	    auto &clauses = id_to_predicate_[index_id];
	    size_t from_clause = bpval & 0xff;

  	    ok = select_clause(code_point(register_qr_), index_id, clauses, from_clause);

	}
	if (!ok) {
	    unbound = false;
	    set_b(ch->b);
	}
    } while (!ok);

    if (!unbound) {
	unwind(current_tr);
    }
}

}}


