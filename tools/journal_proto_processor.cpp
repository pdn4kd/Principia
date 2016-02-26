﻿
#include "tools/journal_proto_processor.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "glog/logging.h"
#include "serialization/journal.pb.h"

namespace principia {
namespace tools {

namespace {

char const kMethod[] = "Method";
char const kIn[] = "In";
char const kReturn[] = "Return";
char const kOut[] = "Out";

template<typename Container>
bool Contains(Container const& container,
              typename Container::key_type const key) {
  return container.find(key) != container.end();
}

std::string Join(std::vector<std::string> const& v, std::string const& joiner) {
  std::string joined;
  bool is_first = true;
  for (auto const vi : v) {
    if (vi.empty()) {
      continue;
    }
    if (is_first) {
      is_first = false;
      joined = vi;
    } else {
      joined += joiner + vi;
    }
  }
  return joined;
}

std::string ToLower(std::string const& s) {
  std::string lower;
  for (int i = 0; i < s.size(); ++i) {
    if (i > 0 && i < s.size() - 1 &&
        std::isupper(s[i]) && std::islower(s[i + 1])) {
      lower += "_" + std::string(1, std::tolower(s[i]));
    } else {
      lower += std::tolower(s[i]);
    }
  }
  return lower;
}

}  // namespace

void JournalProtoProcessor::ProcessMessages() {
  // Get the file containing |Method|.
  Descriptor const* method_descriptor = serialization::Method::descriptor();
  FileDescriptor const* file_descriptor = method_descriptor->file();

  // Process all the messages in that file.
  for (int i = 0; i < file_descriptor->message_type_count(); ++i) {
    Descriptor const* message_descriptor = file_descriptor->message_type(i);
    std::string message_descriptor_name = message_descriptor->name();
    if (message_descriptor->extension_range_count() > 0) {
      // Only the |Method| message should have a range.  Don't generate any code
      // for it.
      CHECK_EQ(kMethod, message_descriptor_name)
          << message_descriptor_name << " should not have extension ranges";
      continue;
    }
    switch (message_descriptor->extension_count()) {
      case 0: {
        // A message corresponding to a struct interchanged through the
        // interface.
        ProcessInterchangeMessage(message_descriptor);
        break;
      }
      case 1: {
        // An extension.  Check that it extends |Method|.
        FieldDescriptor const* extension = message_descriptor->extension(0);
        CHECK(extension->is_extension());
        Descriptor const* containing_type = extension->containing_type();
        CHECK_EQ(method_descriptor, containing_type)
            << message_descriptor_name << " extends a message other than "
            << method_descriptor->name() << ": " << containing_type->name();
        ProcessMethodExtension(message_descriptor);
        break;
      }
      default: {
        LOG(FATAL) << message_descriptor_name << " has "
                   << message_descriptor->extension_count() << " extensions";
      }
    }
  }
}

std::vector<std::string>
JournalProtoProcessor::GetCsInterfaceMethodDeclarations() const {
  std::vector<std::string> result;
  for (auto const& pair : cs_interface_method_declaration_) {
    result.push_back(pair.second);
  }
  return result;
}

std::vector<std::string>
JournalProtoProcessor::GetCsInterfaceTypeDeclarations() const {
  std::vector<std::string> result;
  for (auto const& pair : cs_interface_type_declaration_) {
    result.push_back(pair.second);
  }
  return result;
}

std::vector<std::string>
JournalProtoProcessor::GetCxxInterfaceMethodDeclarations() const {
  std::vector<std::string> result;
  for (auto const& pair : cxx_interface_method_declaration_) {
    result.push_back(pair.second);
  }
  return result;
}

std::vector<std::string>
JournalProtoProcessor::GetCxxInterfaceTypeDeclarations() const {
  std::vector<std::string> result;
  for (auto const& pair : cxx_interface_type_declaration_) {
    result.push_back(pair.second);
  }
  return result;
}

std::vector<std::string>
JournalProtoProcessor::GetCxxInterchangeImplementations() const {
  std::vector<std::string> result;
  result.push_back("namespace {\n\n");
  for (auto const& pair : cxx_deserialize_definition_) {
    result.push_back(pair.second);
  }
  for (auto const& pair : cxx_serialize_definition_) {
    result.push_back(pair.second);
  }
  result.push_back("}  // namespace\n\n");
  return result;
}

std::vector<std::string>
JournalProtoProcessor::GetCxxMethodImplementations() const {
  std::vector<std::string> result;
  for (auto const& pair : cxx_functions_implementation_) {
    result.push_back(pair.second);
  }
  return result;
}

std::vector<std::string> JournalProtoProcessor::GetCxxMethodTypes() const {
  std::vector<std::string> result;
  for (auto const& pair : cxx_toplevel_type_declaration_) {
    result.push_back(pair.second);
  }
  return result;
}

std::vector<std::string> JournalProtoProcessor::GetCxxPlayStatements() const {
  std::vector<std::string> result;
  result.push_back("{\n  bool ran = false;\n");
  for (auto const& pair : cxx_play_statement_) {
    result.push_back(pair.second);
  }
  result.push_back("  CHECK(ran) << method->DebugString();\n}\n");
  return result;
}

void JournalProtoProcessor::ProcessRepeatedMessageField(
    FieldDescriptor const* descriptor) {
  std::string const& message_type_name = descriptor->message_type()->name();

  FieldOptions const& options = descriptor->options();
  CHECK(options.HasExtension(serialization::size))
      << descriptor->full_name() << " is missing a (size) option";
  size_member_name_[descriptor] = options.GetExtension(serialization::size);
  field_cs_type_[descriptor] = message_type_name + "[]";
  field_cxx_type_[descriptor] = message_type_name + " const*";

  field_cxx_arguments_fn_[descriptor] =
      [](std::string const& identifier) -> std::vector<std::string> {
        return {"&" + identifier + "[0]", identifier + ".size()"};
      };
  field_cxx_assignment_fn_[descriptor] =
      [this, descriptor, message_type_name](
          std::string const& prefix, std::string const& expr) {
        std::string const& descriptor_name = descriptor->name();
        // The use of |substr| below is a bit of a cheat because we known the
        // structure of |expr|.
        return "  for (" + message_type_name + " const* " + descriptor_name +
               " = " + expr + "; " + descriptor_name + " < " + expr + " + " +
               expr.substr(0, expr.find('.')) + "." +
               size_member_name_[descriptor] + "; ++" + descriptor_name +
               ") {\n    *" + prefix + "add_" + descriptor_name +
               "() = " +
               field_cxx_serializer_fn_[descriptor]("*"+ descriptor_name) +
               ";\n  }\n";
      };
  field_cxx_deserializer_fn_[descriptor] =
      [descriptor, message_type_name](std::string const& expr) {
        std::string const& descriptor_name = descriptor->name();
        // Yes, this lambda generates a lambda.
        return "[](::google::protobuf::RepeatedPtrField<serialization::" +
               message_type_name + "> const& messages) -> std::vector<" +
               message_type_name + "> {\n"
               "      std::vector<" + message_type_name + "> deserialized_" +
               descriptor_name + ";\n" +
               "      for (auto const& message : messages) {\n" +
               "        deserialized_" + descriptor_name +
               ".push_back(Deserialize" + message_type_name + "(message));\n" +
               "      }\n"
               "      return deserialized_" + descriptor_name +
               ";\n    }(" + expr + ")";
      };
  field_cxx_serializer_fn_[descriptor] =
      [message_type_name](std::string const& expr) {
        return "Serialize" + message_type_name + "(" + expr + ")";
      };
}

void JournalProtoProcessor::ProcessOptionalInt32Field(
    FieldDescriptor const* descriptor) {
  // It is not possible to use a custom marshaler on an |int?|, as this raises
  // |System.Runtime.InteropServices.MarshalDirectiveException| with the message
  // "Custom marshalers are only allowed on classes, strings, arrays, and boxed
  // value types.".  We could use a boxed |int|, whose type would be |object|,
  // but we would lose static typing.  We use a custom strongly-typed boxed type
  // instead.
  field_cs_type_[descriptor] = "Boxed<int>";
  field_cs_marshal_[descriptor] =
      "[MarshalAs(UnmanagedType.CustomMarshaler, "
      "MarshalTypeRef = typeof(OptionalMarshaler<int>))]";
  field_cxx_type_[descriptor] = "int const*";

  field_cxx_arguments_fn_[descriptor] =
      [](std::string const& identifier) -> std::vector<std::string> {
        return {identifier + ".get()"};
      };
  field_cxx_indirect_member_get_fn_[descriptor] =
      [](std::string const& expr) {
        return "*" + expr;
      };
  field_cxx_optional_pointer_fn_[descriptor] =
      [this, descriptor](std::string const& condition,
                         std::string const& expr) {
        // Tricky.  We need a heap allocation to obtain a pointer to the value.
        return condition + " ? std::make_unique<int const>(" + expr +
               ") : nullptr";
      };
}

void JournalProtoProcessor::ProcessRequiredFixed64Field(
    FieldDescriptor const* descriptor) {
  FieldOptions const& options = descriptor->options();
  CHECK(options.HasExtension(serialization::pointer_to))
      << descriptor->full_name() << " is missing a (pointer_to) option";
  std::string const& pointer_to =
      options.GetExtension(serialization::pointer_to);
  if (options.HasExtension(serialization::is_subject)) {
    CHECK(options.GetExtension(serialization::is_subject))
        << descriptor->full_name() << " has incorrect (is_subject) option";
    field_cs_type_[descriptor] = "this IntPtr";
  } else {
    field_cs_type_[descriptor] = "IntPtr";
  }
  field_cxx_type_[descriptor] = pointer_to + "*";

  if (Contains(out_, descriptor) && !Contains(in_out_, descriptor)) {
    CHECK(!options.HasExtension(serialization::is_consumed) &&
          !options.HasExtension(serialization::is_consumed_if))
        << "out parameter " + descriptor->full_name() + " cannot be consumed";
  }

  if (options.HasExtension(serialization::is_consumed)) {
    CHECK(options.GetExtension(serialization::is_consumed))
        << descriptor->full_name() << " has incorrect (is_consumed) option";
    field_cxx_deleter_fn_[descriptor] =
        [](std::string const& expr) {
          return "  Delete(pointer_map, " + expr + ");\n";
        };
  }
  if (options.HasExtension(serialization::is_consumed_if)) {
    CHECK(!options.HasExtension(serialization::is_consumed))
        << descriptor->full_name()
        << " has incorrect (is_consumed) and (is_consumed_if) options";
    field_cxx_deleter_fn_[descriptor] =
        [options](std::string const& expr) {
          return "  if (" +
                 options.GetExtension(serialization::is_consumed_if) +
                 ") {\n    Delete(pointer_map, " + expr + ");\n  }\n";
        };
  }
  if (options.HasExtension(serialization::is_produced)) {
    CHECK(options.GetExtension(serialization::is_produced))
        << descriptor->full_name() << " has incorrect (is_produced) option";
    field_cxx_inserter_fn_[descriptor] =
        [](std::string const& expr1, std::string const& expr2) {
          return "  Insert(pointer_map, " + expr1 + ", " + expr2 + ");\n";
        };
  }
  if (options.HasExtension(serialization::is_produced_if)) {
    CHECK(!options.HasExtension(serialization::is_produced))
        << descriptor->full_name()
        << " has incorrect (is_produced) and (is_produced_if) options";
    field_cxx_inserter_fn_[descriptor] =
        [options](std::string const& expr1, std::string const& expr2) {
          return "  if (" +
                 options.GetExtension(serialization::is_produced_if) +
                 ") {\n    Insert(pointer_map, " + expr1 + ", " + expr2 +
                 ");\n  }\n";
        };
  }

  field_cxx_deserializer_fn_[descriptor] =
      [pointer_to](std::string const& expr) {
        return "DeserializePointer<" + pointer_to + "*>(*pointer_map, " + expr +
               ")";
      };
  field_cxx_serializer_fn_[descriptor] =
      [](std::string const& expr) {
        return "SerializePointer(" + expr + ")";
      };
}

void JournalProtoProcessor::ProcessRequiredMessageField(
    FieldDescriptor const* descriptor) {
  std::string const& message_type_name = descriptor->message_type()->name();
  field_cs_type_[descriptor] = message_type_name;
  field_cxx_type_[descriptor] = message_type_name;

  field_cxx_assignment_fn_[descriptor] =
      [this, descriptor](std::string const& prefix,
                         std::string const& expr) {
        return "  *" + prefix + "mutable_" + descriptor->name() +
               "() = " + field_cxx_serializer_fn_[descriptor](expr) + ";\n";
      };
  field_cxx_deserializer_fn_[descriptor] =
      [message_type_name](std::string const& expr) {
        return "Deserialize" + message_type_name + "(" + expr + ")";
      };
  field_cxx_serializer_fn_[descriptor] =
      [message_type_name](std::string const& expr) {
        return "Serialize" + message_type_name + "(" + expr + ")";
      };
}

void JournalProtoProcessor::ProcessRequiredBoolField(
    FieldDescriptor const* descriptor) {
  field_cs_type_[descriptor] = "bool";
  field_cxx_type_[descriptor] = descriptor->cpp_type_name();
}

void JournalProtoProcessor::ProcessRequiredDoubleField(
    FieldDescriptor const* descriptor) {
  field_cs_type_[descriptor] = "double";
  field_cxx_type_[descriptor] = descriptor->cpp_type_name();
}

void JournalProtoProcessor::ProcessRequiredInt32Field(
    FieldDescriptor const* descriptor) {
  field_cs_type_[descriptor] = "int";
  field_cxx_type_[descriptor] = "int";
}

void JournalProtoProcessor::ProcessRequiredUint32Field(
    FieldDescriptor const* descriptor) {
  field_cs_type_[descriptor] = "uint";
  field_cxx_type_[descriptor] = "uint32_t";
}

void JournalProtoProcessor::ProcessSingleStringField(
    FieldDescriptor const* descriptor) {
  field_cs_marshal_[descriptor] = "[MarshalAs(UnmanagedType.LPStr)]";
  field_cs_type_[descriptor] = "String";
  field_cxx_type_[descriptor] = "char const*";
  FieldOptions const& options = descriptor->options();
  if (options.HasExtension(serialization::size)) {
    size_member_name_[descriptor] = options.GetExtension(serialization::size);

    field_cxx_arguments_fn_[descriptor] =
        [](std::string const& identifier) -> std::vector<std::string> {
          return {identifier + "->c_str()", identifier + "->size()"};
        };
    field_cxx_deserializer_fn_[descriptor] =
        [](std::string const& expr) {
          return "&" + expr;
        };
    field_cxx_indirect_member_get_fn_[descriptor] =
        [this, descriptor](std::string const& expr) {
          return "std::string(" + expr + ", " +
                 expr.substr(0, expr.find('.')) + "." +
                 size_member_name_[descriptor] + ")";
        };
  } else {
    field_cxx_deserializer_fn_[descriptor] =
        [](std::string const& expr) {
          return expr + ".c_str()";
        };
  }
}

void JournalProtoProcessor::ProcessOptionalField(
    FieldDescriptor const* descriptor) {
  field_cxx_optional_assignment_fn_[descriptor] =
      [](std::string const& expr, std::string const& stmt) {
        return "  if (" + expr + " != nullptr) {\n  " + stmt + "  }\n";
      };
  field_cxx_optional_pointer_fn_[descriptor] =
      [](std::string const& condition, std::string const& expr) {
        return condition + " ? " + expr + " : nullptr";
      };
  switch (descriptor->type()) {
    case FieldDescriptor::TYPE_INT32:
      ProcessOptionalInt32Field(descriptor);
      break;
    case FieldDescriptor::TYPE_STRING:
      ProcessSingleStringField(descriptor);
      break;
    default:
      LOG(FATAL) << descriptor->full_name() << " has unexpected type "
                 << descriptor->type_name();
  }
}

void JournalProtoProcessor::ProcessRepeatedField(
    FieldDescriptor const* descriptor) {
  switch (descriptor->type()) {
  case FieldDescriptor::TYPE_MESSAGE:
      ProcessRepeatedMessageField(descriptor);
      break;
    default:
      LOG(FATAL) << descriptor->full_name() << " has unexpected type "
                 << descriptor->type_name();
  }
}

void JournalProtoProcessor::ProcessRequiredField(
    FieldDescriptor const* descriptor) {
  switch (descriptor->type()) {
    case FieldDescriptor::TYPE_BOOL:
      ProcessRequiredBoolField(descriptor);
      break;
    case FieldDescriptor::TYPE_DOUBLE:
      ProcessRequiredDoubleField(descriptor);
      break;
    case FieldDescriptor::TYPE_FIXED64:
      ProcessRequiredFixed64Field(descriptor);
      break;
    case FieldDescriptor::TYPE_INT32:
      ProcessRequiredInt32Field(descriptor);
      break;
    case FieldDescriptor::TYPE_MESSAGE:
      ProcessRequiredMessageField(descriptor);
      break;
    case FieldDescriptor::TYPE_STRING:
      ProcessSingleStringField(descriptor);
      break;
    case FieldDescriptor::TYPE_UINT32:
      ProcessRequiredUint32Field(descriptor);
      break;
    default:
      LOG(FATAL) << descriptor->full_name() << " has unexpected type "
                 << descriptor->type_name();
  }

  // For in-out fields the data is actually passed with an extra level of
  // indirection.
  if (Contains(in_out_, descriptor) || Contains(out_, descriptor)) {
    field_cxx_arguments_fn_[descriptor] =
        [](std::string const& identifier) -> std::vector<std::string> {
          return {"&" + identifier};
        };
    field_cxx_indirect_member_get_fn_[descriptor] =
        [](std::string const& expr) {
          return "*" + expr;
        };

    if (Contains(in_out_, descriptor)) {
      field_cs_mode_fn_[descriptor] =
          [](std::string const& type) {
            return "ref " + type;
          };
    } else {
      field_cs_mode_fn_[descriptor] =
          [](std::string const& type) {
            return "out " + type;
          };
    }
    field_cxx_mode_fn_[descriptor] =
        [](std::string const& type) {
          return type + "*";
        };
  }
}

void JournalProtoProcessor::ProcessField(FieldDescriptor const* descriptor) {
  // Useful defaults for the lambdas, which ensure that they are set for all
  // fields.  They will be overwritten by actual processing as needed.
  field_cs_mode_fn_[descriptor] =
      [](std::string const& type) {
        return type;
      };
  field_cxx_arguments_fn_[descriptor] =
      [](std::string const& identifier) -> std::vector<std::string> {
        return {identifier};
      };
  field_cxx_assignment_fn_[descriptor] =
      [this, descriptor](std::string const& prefix, std::string const& expr) {
        return "  " + prefix + "set_" + descriptor->name() + "(" +
               field_cxx_serializer_fn_[descriptor](expr) + ");\n";
      };
  field_cxx_indirect_member_get_fn_[descriptor] =
      [](std::string const& expr) {
        return expr;
      };
  field_cxx_deserializer_fn_[descriptor] =
      [](std::string const& expr) {
        return expr;
      };
  field_cxx_mode_fn_[descriptor] =
      [](std::string const& type) {
        return type;
      };
  field_cxx_optional_assignment_fn_[descriptor] =
      [](std::string const& expr, std::string const& stmt) {
        return stmt;
      };
  field_cxx_optional_pointer_fn_[descriptor] =
      [](std::string const& condition, std::string const& expr) {
        return expr;
      };
  field_cxx_serializer_fn_[descriptor] =
      [](std::string const& expr) {
        return expr;
      };

  switch (descriptor->label()) {
    case FieldDescriptor::LABEL_OPTIONAL:
      ProcessOptionalField(descriptor);
      break;
    case FieldDescriptor::LABEL_REPEATED:
      ProcessRepeatedField(descriptor);
      break;
    case FieldDescriptor::LABEL_REQUIRED:
      ProcessRequiredField(descriptor);
      break;
    }
}

void JournalProtoProcessor::ProcessInOut(
  Descriptor const* descriptor,
  std::vector<FieldDescriptor const*>* field_descriptors) {
  std::string const& name = descriptor->name();

  std::string cxx_message_prefix;
  {
    std::string const cxx_message_name =
        "message->mutable_" + ToLower(name) + "()";
    // Generate slightly more compact code in the frequent case where the
    // message only has one field.
    if (descriptor->field_count() > 1) {
      cxx_fill_body_[descriptor] =
          "  auto* const m = " + cxx_message_name + ";\n";
      cxx_message_prefix = "m->";
    } else {
      cxx_message_prefix = cxx_message_name + "->";
      cxx_fill_body_[descriptor].clear();
    }
  }

  cs_interface_parameters_[descriptor].clear();
  cxx_interface_parameters_[descriptor].clear();
  cxx_run_body_prolog_[descriptor] =
      "  auto const& " + ToLower(name) + " = message." +
      ToLower(name) + "();\n";
  cxx_run_arguments_[descriptor].clear();
  cxx_run_body_epilog_[descriptor].clear();

  cxx_nested_type_declaration_[descriptor] = "  struct " + name + " {\n";
  for (int i = 0; i < descriptor->field_count(); ++i) {
    FieldDescriptor const* field_descriptor = descriptor->field(i);
    std::string const& field_descriptor_name = field_descriptor->name();
    if (field_descriptors != nullptr) {
      field_descriptors->push_back(field_descriptor);
    }
    ProcessField(field_descriptor);

    // For in-out parameters, the code is generated only once, on the in
    // occurrence.
    bool const must_generate_code =
        name == kIn || !Contains(in_out_, field_descriptor);

    std::string const cxx_fill_member_name =
        ToLower(name) + "." + field_descriptor_name;
    std::string const cxx_run_field_getter =
        ToLower(name) + "." + field_descriptor_name + "()";
    std::string const run_local_variable = field_descriptor_name;

    cxx_fill_body_[descriptor] +=
        field_cxx_optional_assignment_fn_[field_descriptor](
            cxx_fill_member_name,
            field_cxx_assignment_fn_[field_descriptor](
                cxx_message_prefix,
                field_cxx_indirect_member_get_fn_[field_descriptor](
                    cxx_fill_member_name)));
    std::vector<std::string> const field_arguments =
        field_cxx_arguments_fn_[field_descriptor](run_local_variable);
    if (must_generate_code) {
      std::copy(field_arguments.begin(), field_arguments.end(),
                std::back_inserter(cxx_run_arguments_[descriptor]));
    }
    if (must_generate_code) {
      if (Contains(out_, field_descriptor)) {
        cxx_run_body_prolog_[descriptor] +=
            "  " + field_cxx_type_[field_descriptor] + " " +
            run_local_variable + ";\n";
      } else {
        cxx_run_body_prolog_[descriptor] +=
            "  auto " + run_local_variable + " = " +
            field_cxx_optional_pointer_fn_[field_descriptor](
                ToLower(name) + ".has_" + field_descriptor_name + "()",
                field_cxx_deserializer_fn_[field_descriptor](
                    cxx_run_field_getter)) +
            ";\n";
      }
    }
    if (Contains(field_cxx_deleter_fn_, field_descriptor)) {
      cxx_run_body_epilog_[descriptor] +=
          field_cxx_deleter_fn_[field_descriptor](cxx_run_field_getter);
    }
    if (Contains(field_cxx_inserter_fn_, field_descriptor)) {
      cxx_run_body_epilog_[descriptor] +=
          field_cxx_inserter_fn_[field_descriptor](
              ToLower(name) + "." + field_descriptor_name + "()",
              run_local_variable);
    }

    if (must_generate_code) {
      cs_interface_parameters_[descriptor].push_back(
          "  " + Join({field_cs_marshal_[field_descriptor],
                       field_cs_mode_fn_[field_descriptor](
                           field_cs_type_[field_descriptor])},
                      /*joiner=*/" ") +
          " " + field_descriptor_name);
      cxx_interface_parameters_[descriptor].push_back(
          field_cxx_mode_fn_[field_descriptor](
              field_cxx_type_[field_descriptor]) +
          " const " + field_descriptor_name);
    }
    cxx_nested_type_declaration_[descriptor] +=
        "    " + field_cxx_mode_fn_[field_descriptor](
                     field_cxx_type_[field_descriptor]) +
        " const " + field_descriptor_name + ";\n";

    // If this field has a size, generate it now.
    if (Contains(size_member_name_, field_descriptor)) {
      if (must_generate_code) {
        cs_interface_parameters_[descriptor].push_back(
            "  int " + size_member_name_[field_descriptor]);
        cxx_interface_parameters_[descriptor].push_back(
            "int const " + size_member_name_[field_descriptor]);
      }
      cxx_nested_type_declaration_[descriptor] +=
          "    int const " + size_member_name_[field_descriptor] + ";\n";
    }
  }
  cxx_nested_type_declaration_[descriptor] += "  };\n";
}

void JournalProtoProcessor::ProcessReturn(Descriptor const* descriptor) {
  CHECK_EQ(1, descriptor->field_count())
      << descriptor->full_name() << " must have exactly one field";
  FieldDescriptor const* field_descriptor = descriptor->field(0);
  CHECK_EQ(FieldDescriptor::LABEL_REQUIRED, field_descriptor->label())
      << descriptor->full_name() << " must be required";
  ProcessField(field_descriptor);
  cxx_fill_body_[descriptor] =
      field_cxx_assignment_fn_[field_descriptor]("message->mutable_return_()->",
                                             "result");
  std::string const cxx_field_getter =
      "message.return_()." + field_descriptor->name() + "()";
  if (Contains(field_cxx_inserter_fn_, field_descriptor)) {
    cxx_run_body_epilog_[descriptor] =
        field_cxx_inserter_fn_[field_descriptor](cxx_field_getter, "result");
  } else {
    cxx_run_body_epilog_[descriptor] =
        "  CHECK(" +
        field_cxx_deserializer_fn_[field_descriptor](cxx_field_getter) +
        " == result);\n";
  }
  cs_interface_return_type_[descriptor] =
      Join({field_cs_marshal_[field_descriptor],
            field_cs_type_[field_descriptor]}, /*joiner=*/" ");
  cxx_interface_return_type_[descriptor] = field_cxx_type_[field_descriptor];
  cxx_nested_type_declaration_[descriptor] =
      "  using Return = " + field_cxx_type_[field_descriptor] + ";\n";
}

void JournalProtoProcessor::ProcessInterchangeMessage(
    Descriptor const* descriptor) {
  std::string const& name = descriptor->name();
  std::string const& parameter_name = ToLower(name);

  cxx_deserialize_definition_[descriptor] =
      name + " Deserialize" + name + "(serialization::" + name + " const& " +
      parameter_name + ") {\n  return {";
  cxx_serialize_definition_[descriptor] =
      "serialization::" + name + " Serialize" + name + "(" + name + " const& " +
      parameter_name + ") {\n  serialization::" + name + " m;\n";

  cs_interface_type_declaration_[descriptor] =
      "[StructLayout(LayoutKind.Sequential)]\ninternal partial struct " + name +
      " {\n";
  cxx_interface_type_declaration_[descriptor] =
      "extern \"C\"\nstruct " + name + " {\n";

  std::vector<std::string> deserialized_expressions;
  for (int i = 0; i < descriptor->field_count(); ++i) {
    FieldDescriptor const* field_descriptor = descriptor->field(i);
    std::string const& field_descriptor_name = field_descriptor->name();
    ProcessField(field_descriptor);

    std::string const deserialize_field_getter =
        parameter_name + "." + field_descriptor_name + "()";
    std::string const serialize_member_name =
        parameter_name + "." + field_descriptor_name;
    deserialized_expressions.push_back(
        field_cxx_deserializer_fn_[field_descriptor](deserialize_field_getter));
    cxx_serialize_definition_[descriptor] +=
        field_cxx_assignment_fn_[field_descriptor]("m.", serialize_member_name);

    cs_interface_type_declaration_[descriptor] +=
        "  public " + field_cs_type_[field_descriptor] + " " +
        field_descriptor_name + ";\n";
    cxx_interface_type_declaration_[descriptor] +=
        "  " + field_cxx_type_[field_descriptor] + " " + field_descriptor_name +
        ";\n";
  }
  cxx_deserialize_definition_[descriptor] +=
      Join(deserialized_expressions, /*joiner=*/",\n          ") +  // NOLINT
      "};\n}\n\n";
  cxx_serialize_definition_[descriptor] += "  return m;\n}\n\n";

  cs_interface_type_declaration_[descriptor] += "}\n\n";
  cxx_interface_type_declaration_[descriptor] +=
      "};\n\nstatic_assert(std::is_pod<" + name +
      ">::value,\n              \"" + name + " is used for interfacing\");\n\n";
}

void JournalProtoProcessor::ProcessMethodExtension(
    Descriptor const* descriptor) {
  std::string const& name = descriptor->name();
  bool has_in = false;
  bool has_out = false;
  bool has_return = false;

  // Do a first pass to determine which fields are in-out.  The data produced
  // here will be overwritten by the next pass.
  std::vector<FieldDescriptor const*> field_descriptors;
  for (int i = 0; i < descriptor->nested_type_count(); ++i) {
    Descriptor const* nested_descriptor = descriptor->nested_type(i);
    const std::string& nested_name = nested_descriptor->name();
    if (nested_name == kIn) {
      has_in = true;
      ProcessInOut(nested_descriptor, &field_descriptors);
    } else if (nested_name == kOut) {
      has_out = true;
      std::vector<FieldDescriptor const*> out_field_descriptors;
      ProcessInOut(nested_descriptor, &out_field_descriptors);
      out_.insert(out_field_descriptors.begin(), out_field_descriptors.end());
      std::copy(out_field_descriptors.begin(),
                out_field_descriptors.end(),
                std::back_inserter(field_descriptors));
    } else if (nested_name == kReturn) {
      has_return = true;
    } else {
      LOG(FATAL) << "Unexpected nested message "
          << nested_descriptor->full_name();
    }
  }

  // Now mark the fields that have the same name in In and Out as in-out.
  if (has_in && has_out) {
    std::sort(field_descriptors.begin(),
              field_descriptors.end(),
              [](FieldDescriptor const* left,
                  FieldDescriptor const* right) {
      return left->name() < right->name();
    });
    for (int i = 0; i < field_descriptors.size() - 1; ++i) {
      if (field_descriptors[i]->name() == field_descriptors[i + 1]->name()) {
        in_out_.insert(field_descriptors[i]);
        in_out_.insert(field_descriptors[i + 1]);
      }
    }
  }

  // The second pass that produces the actual output.
  std::vector<std::string> cs_interface_parameters;
  std::vector<std::string> cxx_interface_parameters;
  std::vector<std::string> cxx_run_arguments;
  std::string cs_interface_return_type = "void";
  std::string cxx_interface_return_type = "void";
  std::string cxx_run_prolog;
  std::string cxx_run_epilog;
  cxx_toplevel_type_declaration_[descriptor] = "struct " + name + " {\n";
  for (int i = 0; i < descriptor->nested_type_count(); ++i) {
    Descriptor const* nested_descriptor = descriptor->nested_type(i);
    const std::string& nested_name = nested_descriptor->name();
    if (nested_name == kIn) {
      ProcessInOut(nested_descriptor, /*field_descriptors=*/nullptr);
      cxx_functions_implementation_[descriptor] +=
          "void " + name + "::Fill(In const& in, "
          "not_null<Message*> const message) {\n" +
          cxx_fill_body_[nested_descriptor] +
          "}\n\n";
      cxx_run_prolog += cxx_run_body_prolog_[nested_descriptor];
      std::copy(cs_interface_parameters_[nested_descriptor].begin(),
                cs_interface_parameters_[nested_descriptor].end(),
                std::back_inserter(cs_interface_parameters));
      std::copy(cxx_interface_parameters_[nested_descriptor].begin(),
                cxx_interface_parameters_[nested_descriptor].end(),
                std::back_inserter(cxx_interface_parameters));
      std::copy(cxx_run_arguments_[nested_descriptor].begin(),
                cxx_run_arguments_[nested_descriptor].end(),
                std::back_inserter(cxx_run_arguments));
    } else if (nested_name == kOut) {
      ProcessInOut(nested_descriptor, /*field_descriptors=*/nullptr);
      cxx_functions_implementation_[descriptor] +=
          "void " + name + "::Fill(Out const& out, "
          "not_null<Message*> const message) {\n" +
          cxx_fill_body_[nested_descriptor] +
          "}\n\n";
      cxx_run_prolog += cxx_run_body_prolog_[nested_descriptor];
      std::copy(cs_interface_parameters_[nested_descriptor].begin(),
                cs_interface_parameters_[nested_descriptor].end(),
                std::back_inserter(cs_interface_parameters));
      std::copy(cxx_interface_parameters_[nested_descriptor].begin(),
                cxx_interface_parameters_[nested_descriptor].end(),
                std::back_inserter(cxx_interface_parameters));
      std::copy(cxx_run_arguments_[nested_descriptor].begin(),
                cxx_run_arguments_[nested_descriptor].end(),
                std::back_inserter(cxx_run_arguments));
    } else if (nested_name == kReturn) {
      ProcessReturn(nested_descriptor);
      cxx_functions_implementation_[descriptor] +=
          "void " + name + "::Fill("
          "Return const& result, "
          "not_null<Message*> const message) {\n" +
          cxx_fill_body_[nested_descriptor] +
          "}\n\n";
      cs_interface_return_type = cs_interface_return_type_[nested_descriptor];
      cxx_interface_return_type = cxx_interface_return_type_[nested_descriptor];
    }
    cxx_run_epilog += cxx_run_body_epilog_[nested_descriptor];
    cxx_toplevel_type_declaration_[descriptor] +=
        cxx_nested_type_declaration_[nested_descriptor];
  }
  if (has_in || has_out || has_return) {
    cxx_toplevel_type_declaration_[descriptor] += "\n";
  }
  cxx_toplevel_type_declaration_[descriptor] +=
      "  using Message = serialization::" + name + ";\n";
  if (has_in) {
    cxx_toplevel_type_declaration_[descriptor] +=
        "  static void Fill(In const& in, "
        "not_null<Message*> const message);\n";
  }
  if (has_out) {
    cxx_toplevel_type_declaration_[descriptor] +=
        "  static void Fill(Out const& out, "
        "not_null<Message*> const message);\n";
  }
  if (has_return) {
    cxx_toplevel_type_declaration_[descriptor] +=
        "  static void Fill("
        "Return const& result, "
        "not_null<Message*> const message);\n";
  }
  cxx_toplevel_type_declaration_[descriptor] +=
      "  static void Run("
      "Message const& message,\n"
      "                  not_null<"
      "Player::PointerMap*> const pointer_map);"
      "\n";
  cxx_toplevel_type_declaration_[descriptor] += "};\n\n";

  // The Run method must come after the Fill methods for comparison with manual
  // code.
  cxx_functions_implementation_[descriptor] +=
      "void " + name + "::Run(Message const& message, "
      "not_null<Player::PointerMap*> const pointer_map) {\n" +
      cxx_run_prolog;
  if (has_return) {
    cxx_functions_implementation_[descriptor] += "  auto const result = ";
  } else {
    cxx_functions_implementation_[descriptor] += "  ";
  }
  cxx_functions_implementation_[descriptor] +=
      "interface::principia__" + name + "(" +
      Join(cxx_run_arguments, /*joiner=*/", ") + ");\n";
  cxx_functions_implementation_[descriptor] += cxx_run_epilog + "}\n\n";

  cs_interface_method_declaration_[descriptor] =
      "  [DllImport(dllName           : kDllPath,\n"
      "             EntryPoint        = \"principia__" + name + "\",\n"
      "             CallingConvention = CallingConvention.Cdecl)]\n"
      "  internal static extern " + cs_interface_return_type + " " + name + "(";
  if (!cs_interface_parameters.empty()) {
    cs_interface_method_declaration_[descriptor] +=
        "\n    " + Join(cs_interface_parameters, /*joiner=*/",\n    ");  // NOLINT
  }
  cs_interface_method_declaration_[descriptor] += ");\n\n";

  cxx_interface_method_declaration_[descriptor] =
      "extern \"C\" PRINCIPIA_DLL\n" +
  cxx_interface_return_type + " CDECL principia__" + name + "(";
  if (!cxx_interface_parameters.empty()) {
    cxx_interface_method_declaration_[descriptor] +=
        "\n    " + Join(cxx_interface_parameters, /*joiner=*/",\n    ");  // NOLINT
  }
  cxx_interface_method_declaration_[descriptor] += ");\n\n";

  cxx_play_statement_[descriptor] =
      "  ran |= RunIfAppropriate<" + name + ">(*method);\n";
}

}  // namespace tools
}  // namespace principia
