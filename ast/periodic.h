#pragma once

#include "ast/tree_base.h"

namespace ast {
 
  class Periodic : public Tree_base {
    public:
      Periodic(Node_if& period, Node_if& unit, Node_if& body) 
        : Tree_base(),
          m_period(period),
          m_unit(unit),
          m_body(body) {
        register_branches({&m_period, &m_unit, &m_body});
      }
      
    virtual void visit() {};

    Node_if const& body() const { return m_body; }
    Node_if const& unit() const { return m_unit; }
    Node_if const& period() const { return m_period; }
    
    private:
      Node_if& m_period;
      Node_if& m_unit;
      Node_if& m_body;
  };
  
}
