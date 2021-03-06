//===----------------------------------------------------------------------===//
//
//                         Peloton
//
// bind_node_visitor.cpp
//
// Identification: src/binder/binder_node_visitor.cpp
//
// Copyright (c) 2015-16, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <include/parser/statements.h>
#include <include/expression/tuple_value_expression.h>
#include "parser/select_statement.h"
#include "binder/bind_node_visitor.h"
#include "type/types.h"

namespace peloton {
namespace binder {

BindNodeVisitor::BindNodeVisitor() { context_ = nullptr; }

void BindNodeVisitor::BindNameToNode(parser::SQLStatement *tree) {
  tree->Accept(this);
}

void BindNodeVisitor::Visit(const parser::SelectStatement *node) {
  // Save the upper level context
  auto pre_context = context_;
  context_ = std::make_shared<BinderContext>();
  context_->upper_context = pre_context;

  if (node->from_table != nullptr) node->from_table->Accept(this);
  if (node->where_clause != nullptr) node->where_clause->Accept(this);
  if (node->order != nullptr) node->order->Accept(this);
  if (node->limit != nullptr) node->limit->Accept(this);
  if (node->group_by != nullptr) node->group_by->Accept(this);
  for (auto select_element : *(node->select_list)) {
    select_element->Accept(this);
  }

  // Restore the upper level context
  context_ = context_->upper_context;
}

// Some sub query nodes inside SelectStatement
void BindNodeVisitor::Visit(const parser::JoinDefinition *node) {
  // The columns in join condition can only bind to the join tables
  node->left->Accept(this);
  node->right->Accept(this);
  node->condition->Accept(this);
}

void BindNodeVisitor::Visit(const parser::TableRef *node) {
  // Nested select. Not supported in the current executors
  if (node->select != nullptr) node->select->Accept(this);
  // Join
  else if (node->join != nullptr)
    node->join->Accept(this);
  // Multiple tables
  else if (node->list != nullptr) {
    for (parser::TableRef *table : *(node->list)) table->Accept(this);
  }
  // Single table
  else {
    context_->AddTable(node);
  }
}

void BindNodeVisitor::Visit(const parser::GroupByDescription *node) {
  for (auto col : *(node->columns)) {
    col->Accept(this);
  }
  if (node->having != nullptr) node->having->Accept(this);
}
void BindNodeVisitor::Visit(const parser::OrderDescription *node) {
  if (node->expr != nullptr) node->expr->Accept(this);
}

void BindNodeVisitor::Visit(const parser::UpdateStatement *node) {
  context_ = std::make_shared<BinderContext>();

  node->table->Accept(this);
  if (node->where != nullptr) node->where->Accept(this);

  // TODO: Update columns are not bound because they are char*
  // not TupleValueExpression in update_statement.h

  context_ = nullptr;
}

void BindNodeVisitor::Visit(const parser::DeleteStatement *node) {
  context_ = std::make_shared<BinderContext>();

  context_->AddTable(node->GetDatabaseName(), node->GetTableName());

  if (node->expr != nullptr) node->expr->Accept(this);

  context_ = nullptr;
}

void BindNodeVisitor::Visit(const parser::LimitDescription *) {}
void BindNodeVisitor::Visit(const parser::CopyStatement *) {}
void BindNodeVisitor::Visit(const parser::CreateStatement *) {}
void BindNodeVisitor::Visit(const parser::InsertStatement *) {}
void BindNodeVisitor::Visit(const parser::DropStatement *) {}
void BindNodeVisitor::Visit(const parser::PrepareStatement *) {}
void BindNodeVisitor::Visit(const parser::ExecuteStatement *) {}
void BindNodeVisitor::Visit(const parser::TransactionStatement *) {}

void BindNodeVisitor::Visit(expression::TupleValueExpression *expr) {
  if (!expr->is_bound) {
    std::tuple<oid_t, oid_t, oid_t> col_pos_tuple;
    std::tuple<oid_t, oid_t> table_id_tuple;

    std::string table_name = expr->GetTableName();
    std::string col_name = expr->GetColumnName();

    // Convert all the names to lower cases
    std::transform(table_name.begin(), table_name.end(), table_name.begin(),
                   ::tolower);
    std::transform(col_name.begin(), col_name.end(), col_name.begin(),
                   ::tolower);

    // Table name not specified in the expression
    if (table_name.empty()) {
      if (!BinderContext::GetColumnPosTuple(context_, col_name, col_pos_tuple,
                                            table_name))
        throw Exception("Cannot find column " + col_name);
      else
        expr->SetTableName(table_name);
    }
    // Table name is present
    else if (!BinderContext::GetTableIdTuple(context_, table_name,
                                             &table_id_tuple)) {
      throw Exception("Invalid table reference " + expr->GetTableName());
    } else {
      if (!BinderContext::GetColumnPosTuple(col_name, table_id_tuple,
                                            col_pos_tuple))
        throw Exception("Cannot find column " + col_name);
    }

    expr->SetBoundObjectId(col_pos_tuple);
    expr->is_bound = true;
  }
}

}  // binder
}  // peloton