%module xapimg
%{
#include "imgseek.h"
%}
#define XAPIAN_VISIBILITY_DEFAULT

%include "stl.i"
%include "std_set.i"
%include "std_string.i"
%feature("notabstract") ImgSigSimilarityPostingSource;
%import "xapian/postingsource.h"
%apply const std::string& {std::string& serialised};
namespace std {
  %template(dset) set<double>;
};

%include "xapian/error.h"

%include "exception.i"
%exception {
  try {
    $action
      }
  catch (Xapian::InvalidArgumentError) {
    SWIG_exception(SWIG_RuntimeError, "Bad argument");
      }
 }
%include "imgseek.h"
