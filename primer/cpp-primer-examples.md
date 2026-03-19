document:
  name: cpp-primer-examples
  format: toon
  version: 1

intent:
  summary:
    Provide small example pairs that demonstrate the allowed C++ subset in practice.
    These examples are normative anchors for authors.
  audience:
    primary:
      LLM authors
    secondary:
      human contributors

examples:
  ownership:
    summary:
      Ownership must be obvious from local code.

    good_unique_ptr_factory:
      why:
        Heap ownership is explicit and exclusive.
      code: |
        class Mesh
        {
        public:
            explicit Mesh(std::vector<float> vertices)
                : vertices_(std::move(vertices))
            {
            }

        private:
            std::vector<float> vertices_;
        };

        std::unique_ptr<Mesh> MakeTriangleMesh()
        {
            std::vector<float> vertices = {
                0.0f, 0.0f, 0.0f,
                1.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f
            };

            return std::make_unique<Mesh>(std::move(vertices));
        }

    good_non_owning_reference:
      why:
        The callee requires a valid object but does not own it.
      code: |
        class Logger
        {
        public:
            void Info(const std::string& message);
        };

        void ExportScene(Logger& logger, const std::string& path)
        {
            logger.Info("Exporting scene to " + path);
        }

    good_non_owning_nullable_pointer:
      why:
        Null is meaningful, and ownership is not transferred.
      code: |
        class Material
        {
        public:
            const std::string& Name() const;
        };

        void PrintMaterialName(const Material* material)
        {
            if (material == nullptr)
            {
                std::cout << "No material\n";
                return;
            }

            std::cout << material->Name() << "\n";
        }

    bad_raw_owning_pointer:
      why:
        Ownership is ambiguous and cleanup is easy to forget.
      code: |
        Mesh* MakeTriangleMesh()
        {
            return new Mesh({0.0f, 0.0f, 0.0f});
        }

    bad_shared_ptr_by_default:
      why:
        Shared ownership is introduced without proving it is needed.
      code: |
        std::shared_ptr<Mesh> MakeTriangleMesh()
        {
            return std::make_shared<Mesh>(std::vector<float>{0.0f, 0.0f, 0.0f});
        }

  error_handling:
    summary:
      Failure should be explicit in the signature and visible at the call site.

    good_optional_lookup:
      why:
        Absence is the only failure mode.
      code: |
        std::optional<int> FindNodeIndex(const std::vector<std::string>& names, const std::string& target)
        {
            for (std::size_t i = 0; i < names.size(); ++i)
            {
                if (names[i] == target)
                {
                    return static_cast<int>(i);
                }
            }

            return std::nullopt;
        }

        void Example()
        {
            std::vector<std::string> names = {"root", "arm", "leg"};
            std::optional<int> index = FindNodeIndex(names, "arm");

            if (!index.has_value())
            {
                std::cout << "Node not found\n";
                return;
            }

            std::cout << "Found at index " << *index << "\n";
        }

    good_result_object:
      why:
        The caller needs error details.
      code: |
        struct ParseNumberResult
        {
            bool success;
            int value;
            std::string error;
        };

        ParseNumberResult ParsePositiveInt(const std::string& text)
        {
            if (text.empty())
            {
                return {false, 0, "input is empty"};
            }

            for (char ch : text)
            {
                if (ch < '0' || ch > '9')
                {
                    return {false, 0, "input contains non-digit characters"};
                }
            }

            int value = std::stoi(text);
            if (value <= 0)
            {
                return {false, 0, "value must be positive"};
            }

            return {true, value, ""};
        }

    good_assert_for_invariant:
      why:
        Assertions are for programmer errors and violated invariants, not routine runtime failures.
      code: |
        class BufferView
        {
        public:
            BufferView(const std::vector<std::byte>& data, std::size_t offset, std::size_t size)
                : data_(data), offset_(offset), size_(size)
            {
                assert(offset_ + size_ <= data_.size());
            }

        private:
            const std::vector<std::byte>& data_;
            std::size_t offset_;
            std::size_t size_;
        };

    bad_exception_throw:
      why:
        Exceptions are banned in this subset.
      code: |
        int ParsePositiveInt(const std::string& text)
        {
            if (text.empty())
            {
                throw std::runtime_error("input is empty");
            }

            return std::stoi(text);
        }

    bad_hidden_failure:
      why:
        Logging is not error handling, and hidden side channels are hard to test.
      code: |
        int ParsePositiveInt(const std::string& text)
        {
            if (text.empty())
            {
                std::cerr << "input is empty\n";
                return 0;
            }

            return std::stoi(text);
        }

  composition_vs_inheritance:
    summary:
      Composition is the default. Inheritance is a rare tool for genuine runtime polymorphism.

    good_composition:
      why:
        Small parts are combined directly, with obvious ownership and behavior.
      code: |
        class Transform
        {
        public:
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        class MeshRenderer
        {
        public:
            void Draw() const;
        };

        class Entity
        {
        public:
            Transform transform;
            std::unique_ptr<MeshRenderer> renderer;
        };

    good_runtime_interface_when_needed:
      why:
        Runtime polymorphism is used only when there is a real interface boundary.
      code: |
        class IFileSystem
        {
        public:
            virtual ~IFileSystem() = default;
            virtual std::optional<std::string> ReadTextFile(const std::string& path) = 0;
        };

        class DiskFileSystem final : public IFileSystem
        {
        public:
            std::optional<std::string> ReadTextFile(const std::string& path) override;
        };

    bad_speculative_hierarchy:
      why:
        Inheritance is introduced for hypothetical reuse instead of a concrete need.
      code: |
        class Object
        {
        public:
            virtual ~Object() = default;
        };

        class RenderObject : public Object
        {
        public:
            virtual void Render() = 0;
        };

        class PhysicsRenderObject : public RenderObject
        {
        public:
            virtual void Simulate() = 0;
        };

        class EnemyBossObject final : public PhysicsRenderObject
        {
        public:
            void Render() override;
            void Simulate() override;
        };

  templates:
    summary:
      Templates may remove obvious duplication, but may not introduce compile-time magic.

    good_simple_template:
      why:
        The template directly removes obvious duplication and remains easy to read.
      code: |
        template <typename T>
        T Clamp(T value, T min_value, T max_value)
        {
            if (value < min_value)
            {
                return min_value;
            }

            if (value > max_value)
            {
                return max_value;
            }

            return value;
        }

    good_small_generic_helper:
      why:
        The type parameter is simple and the behavior is obvious.
      code: |
        template <typename T>
        bool Contains(const std::vector<T>& items, const T& value)
        {
            for (const T& item : items)
            {
                if (item == value)
                {
                    return true;
                }
            }

            return false;
        }

    bad_template_machinery:
      why:
        This increases abstraction cost far more than it reduces duplication.
      code: |
        template <typename T, typename Enable = void>
        struct ValueAdapter;

        template <typename T>
        struct ValueAdapter<T, std::enable_if_t<std::is_integral_v<T>>>
        {
            static constexpr bool kIsFastPath = true;
            static T Convert(T value) { return value; }
        };

        template <typename T>
        struct ValueAdapter<T, std::enable_if_t<!std::is_integral_v<T>>>
        {
            static constexpr bool kIsFastPath = false;
            static T Convert(const T& value) { return T(value); }
        };

    bad_compile_time_showboating:
      why:
        Advanced metaprogramming is not a default repository tool.
      code: |
        template <typename... Ts>
        struct TypeList
        {
        };

        template <typename List>
        struct Front;

        template <typename Head, typename... Tail>
        struct Front<TypeList<Head, Tail...>>
        {
            using Type = Head;
        };

  interfaces:
    summary:
      Interfaces should make cost, ownership, and behavior obvious.

    good_explicit_interface:
      why:
        Inputs, outputs, and failure are visible.
      code: |
        struct SaveDocumentResult
        {
            bool success;
            std::string error;
        };

        SaveDocumentResult SaveDocument(const std::string& path, const std::string& content)
        {
            if (path.empty())
            {
                return {false, "path is empty"};
            }

            // Save logic here.
            return {true, ""};
        }

    bad_surprise_work:
      why:
        The function name and signature hide allocation and expensive work.
      code: |
        std::string GetName();

    bad_output_parameter_style_for_primary_result:
      why:
        The main output is less obvious than a direct return value.
      code: |
        bool ParseConfig(const std::string& text, Config& out_config);

  headers_and_includes:
    summary:
      Public interfaces should stay narrow and dependency surfaces should stay small.

    good_header:
      why:
        The header declares an interface without dragging in unrelated implementation details.
      code: |
        #pragma once

        #include <memory>
        #include <string>

        class Mesh;

        class Scene
        {
        public:
            explicit Scene(std::string name);

            const std::string& Name() const;
            void SetMesh(std::unique_ptr<Mesh> mesh);

        private:
            std::string name_;
            std::unique_ptr<Mesh> mesh_;
        };

    bad_header_dumping_ground:
      why:
        The header mixes unrelated utilities, implementation details, macros, and broad includes.
      code: |
        #pragma once

        #include <algorithm>
        #include <array>
        #include <filesystem>
        #include <functional>
        #include <iostream>
        #include <map>
        #include <memory>
        #include <optional>
        #include <set>
        #include <thread>
        #include <unordered_map>
        #include <vector>

        #define SCENE_LOG(x) std::cout << x << std::endl

        template <typename T>
        void DebugAnything(const T& value)
        {
            std::cout << value << std::endl;
        }

        class Scene
        {
        public:
            std::unordered_map<std::string, std::vector<std::function<void()>>> callbacks;
        };

closing:
  message:
    These examples are anchors, not decoration.
    When unsure, prefer the pattern that looks more like the good examples and less like the bad ones.
    The goal is not impressive C++.
    The goal is trustworthy C++.
