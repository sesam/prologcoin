#include "interpreter_base.hpp"
#include "locale.hpp"

namespace prologcoin { namespace interp {

locale::locale(interpreter_base &intp) 
    : interp_(intp)
{
    alias_ = interp_.functor("default",0);
    set_from_platform();
}

locale::locale(interpreter_base &intp, const std::string &name)
  : interp_(intp)
{
    alias_ = interp_.functor(name, 0);
}

void locale::set_from_platform()
{
    std::locale loc("");

    auto &fac = std::use_facet<std::moneypunct<char> >(loc);

    std::string decp;
    decp += fac.decimal_point();
    std::string sep;
    sep += fac.thousands_sep();
    std::string grp = fac.grouping();

    decimal_point_ = interp_.functor(decp, 0);
    thousands_sep_ = interp_.functor(sep, 0);

    for (auto ch : grp) {
	grouping_.push_back((int)ch);
    }
    grouping_.back() *= -1;
}

}}

