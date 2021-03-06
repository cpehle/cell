#include "ast/function_def.h"

#include "ast/qualified_name.h"
#include <iterator>
#include <algorithm>

namespace ast {

	static Qualified_name default_return_type = Qualified_name(
            new Identifier("unit")
        );

	Function_def::Function_def(Node_if& identifier)
		:	Tree_base(),
			m_identifier(identifier),
 			m_return_type(default_return_type) {
        register_branches({&m_identifier, &default_return_type});
        register_branch_lists({&m_parameters});
	}

	Function_def::Function_def(Node_if& identifier, Node_if& return_type)
		:	Tree_base(),
			m_identifier(identifier),
 			m_return_type(return_type) {
        register_branches({&identifier, &return_type});
        register_branch_lists({&m_parameters});
	}

	void
	Function_def::append_parameter(Node_if& node) {
		m_parameters.push_back(&node);
	}

	void
	Function_def::append_parameter(std::vector<Node_if*> const& nodes) {
		std::copy(begin(nodes), end(nodes), std::back_inserter(m_parameters));
	}


	void
	Function_def::append_body(Node_if& node) {
		m_body = &node;
        register_branches({&m_identifier, &default_return_type, m_body});
	}

}


/* vim: set et fenc=utf-8 ff=unix sts=0 sw=4 ts=4 : */
