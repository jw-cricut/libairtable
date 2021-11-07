#pragma once

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <phosg/JSON.hh>



uint64_t parse_airtable_time(const std::string& time);
std::string format_airtable_time(uint64_t time);



class Field {
public:
  enum class ValueType {
    String,
    Integer,
    Float,
    Checkbox,
    Collaborator,
    CollaboratorArray,
    Button,
    StringArray,
    NumberArray, // Always floats (for now)
    AttachmentArray,
  };

  ValueType type;

  virtual std::shared_ptr<JSONObject> to_json() const = 0;

  virtual ~Field() = default;

protected:
  explicit Field(ValueType type);
};

class StringField : public Field {
public:
  std::string value;

  StringField();
  explicit StringField(const std::string& value);
  explicit StringField(std::string&& value);
  virtual ~StringField() = default;
  virtual std::shared_ptr<JSONObject> to_json() const;
};

class IntegerField : public Field {
public:
  int64_t value;

  IntegerField();
  explicit IntegerField(int64_t value);
  virtual ~IntegerField() = default;
  virtual std::shared_ptr<JSONObject> to_json() const;
};

class FloatField : public Field {
public:
  double value;

  FloatField();
  explicit FloatField(double value);
  virtual ~FloatField() = default;
  virtual std::shared_ptr<JSONObject> to_json() const;
};

class ButtonField : public Field {
public:
  std::string url;
  std::string label;

  ButtonField();
  ButtonField(const std::string& url, const std::string& label);
  ButtonField(std::string&& url, std::string&& label);
  virtual ~ButtonField() = default;
  virtual std::shared_ptr<JSONObject> to_json() const;
};

class CheckboxField : public Field {
public:
  bool value;

  CheckboxField();
  explicit CheckboxField(bool value);
  virtual ~CheckboxField() = default;
  virtual std::shared_ptr<JSONObject> to_json() const;
};

class StringArrayField : public Field {
public:
  std::vector<std::string> value;

  StringArrayField();
  explicit StringArrayField(const std::vector<std::string>& value);
  explicit StringArrayField(std::vector<std::string>&& value);
  virtual ~StringArrayField() = default;
  virtual std::shared_ptr<JSONObject> to_json() const;
};

class NumberArrayField : public Field {
public:
  std::vector<double> value;

  NumberArrayField();
  explicit NumberArrayField(const std::vector<double>& value);
  explicit NumberArrayField(std::vector<double>&& value);
  virtual ~NumberArrayField() = default;
  virtual std::shared_ptr<JSONObject> to_json() const;
};

class CollaboratorField : public Field {
public:
  std::string name;
  std::string email;
  std::string user_id;

  CollaboratorField();
  CollaboratorField(
      const std::string& name,
      const std::string& email,
      const std::string& user_id);
  CollaboratorField(
      std::string&& name,
      std::string&& email,
      std::string&& user_id);
  virtual ~CollaboratorField() = default;
  virtual std::shared_ptr<JSONObject> to_json() const;
};

class MultiCollaboratorField : public Field {
public:
  std::vector<CollaboratorField> value;

  MultiCollaboratorField();
  explicit MultiCollaboratorField(const std::vector<CollaboratorField>& value);
  explicit MultiCollaboratorField(std::vector<CollaboratorField>&& value);
  virtual ~MultiCollaboratorField() = default;
  virtual std::shared_ptr<JSONObject> to_json() const;
};

struct Attachment {
  std::string mime_type;
  size_t size;
  std::string filename;
  std::string url;
  std::string attachment_id;
  size_t width; // Zero if not an image
  size_t height; // Zero if not an image
  struct Thumbnail {
    size_t width;
    size_t height;
    std::string url;
  };
  std::unordered_map<std::string, Thumbnail> thumbnails; // Empty if not an image

  Attachment(std::shared_ptr<JSONObject> json);
  std::shared_ptr<JSONObject> to_json() const;
};

class AttachmentField : public Field {
public:
  std::vector<Attachment> value;

  AttachmentField();
  explicit AttachmentField(const std::vector<Attachment>& value);
  explicit AttachmentField(std::vector<Attachment>&& value);
  virtual ~AttachmentField() = default;
  virtual std::shared_ptr<JSONObject> to_json() const;
};



struct Record {
  char id[18]; // always 17 chars long (+ \0)
  uint64_t creation_time;
  std::unordered_map<std::string, std::shared_ptr<Field>> fields;

  Record() = default;
  Record(std::shared_ptr<const JSONObject> json);

  static std::shared_ptr<Field> parse_field(std::shared_ptr<JSONObject> json);

  static std::shared_ptr<JSONObject> json_for_create(
      const std::unordered_map<std::string, std::shared_ptr<Field>>& fields);
  std::shared_ptr<JSONObject> json_for_create() const;
  std::shared_ptr<JSONObject> json_for_update() const;

  std::string str() const;
};



struct TableSchema {
  std::string name;
  std::string primary_field_id;

  struct FieldSchema {
    std::string name;
    std::string type;
    // TODO: probably we should parse these in the future
    std::shared_ptr<JSONObject> options;
  };
  std::unordered_map<std::string, FieldSchema> fields;

  struct ViewSchema {
    std::string name;
    std::string type;
  };
  std::unordered_map<std::string, ViewSchema> views;
};



struct BaseInfo {
  std::string base_id;
  std::string name;
  std::string permission_level;
};
