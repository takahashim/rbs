require "test_helper"

class RBS::RuntimePrototypeTest < Test::Unit::TestCase
  Runtime = RBS::Prototype::Runtime
  DefinitionBuilder = RBS::DefinitionBuilder

  include TestHelper

  module TestTargets
    module Foo
      include Enumerable

      extend Comparable
    end

    module Bar
    end

    class Test < String
      include Foo
      extend Bar

      NAME = "Hello"

      def foo(foo, bar=1, *baz, a, b:, c: nil, **d)
      end

      alias bar foo

      private

      def a()
      end

      def self.baz(&block)
      end

      def self.b()
      end
    end
  end

  def test_1
    SignatureManager.new do |manager|
      manager.build do |env|
        p = Runtime.new(patterns: ["RBS::RuntimePrototypeTest::TestTargets::*"], env: env, merge: false)

        assert_write p.decls, <<-EOF
module RBS
  module RuntimePrototypeTest
    module TestTargets
      module Bar
      end

      module Foo
        include Enumerable[untyped]

        extend Comparable
      end

      class Test < String
        include RBS::RuntimePrototypeTest::TestTargets::Foo

        extend RBS::RuntimePrototypeTest::TestTargets::Bar

        def self.b: () -> untyped

        def self.baz: () { (*untyped) -> untyped } -> untyped

        public

        alias bar foo

        def foo: (untyped foo, ?untyped bar, *untyped baz, untyped a, b: untyped, ?c: untyped, **untyped) -> untyped

        private

        def a: () -> untyped

        NAME: String
      end
    end
  end
end
        EOF
      end
    end
  end

  def test_merge_types
    SignatureManager.new do |manager|
      manager.files[Pathname("foo.rbs")] = <<EOF
class RBS
  class RuntimePrototypeTest
    class TestTargets
      class Test
        def self.baz: () -> void

        def foo: (String) -> Integer
               | (String, bool) { () -> void } -> [Symbol]

        def bar: () -> void
      end
    end
  end
end
EOF

      manager.build do |env|
        p = Runtime.new(patterns: ["RBS::RuntimePrototypeTest::TestTargets::*"], env: env, merge: true)

        assert_write p.decls, <<-EOF
module RBS
  module RuntimePrototypeTest
    module TestTargets
      module Bar
      end

      module Foo
        include Enumerable[untyped]

        extend Comparable
      end

      class Test < String
        include RBS::RuntimePrototypeTest::TestTargets::Foo

        extend RBS::RuntimePrototypeTest::TestTargets::Bar

        def self.b: () -> untyped

        def self.baz: () { (*untyped) -> untyped } -> untyped

        public

        alias bar foo

        def foo: (untyped foo, ?untyped bar, *untyped baz, untyped a, b: untyped, ?c: untyped, **untyped) -> untyped

        private

        def a: () -> untyped

        NAME: String
      end
    end
  end
end
        EOF
      end
    end
  end

  module IncludeTests
    class SuperClass
      def foo; end
      def self.foo; end
    end

    class ChildClass < SuperClass
      def bar; end
    end
  end

  def test_include_owner
    SignatureManager.new do |manager|
      manager.build do |env|
        p = Runtime.new(patterns: ["RBS::RuntimePrototypeTest::IncludeTests::*"],
                        env: env,
                        merge: true,
                        owners_included: ["RBS::RuntimePrototypeTest::IncludeTests::SuperClass"])

        assert_write p.decls, <<-EOF
module RBS
  module RuntimePrototypeTest
    module IncludeTests
      class ChildClass < RBS::RuntimePrototypeTest::IncludeTests::SuperClass
        def self.foo: () -> untyped

        public

        def bar: () -> untyped

        def foo: () -> untyped
      end

      class SuperClass
        def self.foo: () -> untyped

        public

        def foo: () -> untyped
      end
    end
  end
end
        EOF
      end
    end
  end

  module TestForAnonymous
    class C < Class.new
    end

    class C2
      include Module.new
    end

    module M
      CONST = Class.new.new
    end

    module M2
      include Module.new
    end

    module M3
      def self.name
        raise
      end
    end
  end

  def test_decls_for_anonymous_class_or_module
    p = Runtime.new(patterns: ["RBS::RuntimePrototypeTest::TestForAnonymous::*"],
                    env: nil, merge: false)
    silence_warnings do
      p.decls
    end
    assert(true) # nothing raised above
  end

  if RUBY_VERSION >= '2.7' && RUBY_VERSION <= '3.0'
    class TestForArgumentForwarding
      eval <<~RUBY
        def foo(...)
        end
      RUBY
    end

    def test_argument_forwarding
      SignatureManager.new do |manager|
        manager.build do |env|
          p = Runtime.new(patterns: ["RBS::RuntimePrototypeTest::TestForArgumentForwarding"], env: env, merge: true)

          assert_write p.decls, <<-EOF
module RBS
  module RuntimePrototypeTest
    class TestForArgumentForwarding
      public

      def foo: (*untyped) { (*untyped) -> untyped } -> untyped
    end
  end
end
          EOF
        end
      end
    end
  end

  module TestForOverrideModuleName
    module M
      def self.name() 'FakeNameM' end
      def self.to_s() 'FakeToS' end
      X = 1
    end

    class C
      def self.name() 'FakeNameC' end
      def self.to_s() 'FakeToS2' end
      include M

      INSTANCE = C.new
    end


    class C2 < C
    end
  end

  def test_for_overwritten_module_name
    SignatureManager.new do |manager|
      manager.build do |env|
        p = Runtime.new(patterns: ["RBS::RuntimePrototypeTest::TestForOverrideModuleName::*"], env: env, merge: true)

        assert_write p.decls, <<~RBS
          module RBS
            module RuntimePrototypeTest
              module TestForOverrideModuleName
                class C
                  include RBS::RuntimePrototypeTest::TestForOverrideModuleName::M

                  def self.name: () -> untyped

                  def self.to_s: () -> untyped

                  INSTANCE: C
                end

                class C2 < RBS::RuntimePrototypeTest::TestForOverrideModuleName::C
                end

                module M
                  def self.name: () -> untyped

                  def self.to_s: () -> untyped

                  X: Integer
                end
              end
            end
          end
        RBS
      end
    end
  end

  module TestForTypeParameters
    module M
      HASH = { foo: 42 }
    end

    class C < Hash
    end

    class C2
      include Enumerable
    end
  end

  def test_for_type_parameters
    SignatureManager.new do |manager|
      manager.build do |env|
        p = Runtime.new(patterns: ["RBS::RuntimePrototypeTest::TestForTypeParameters::*"], env: env, merge: true)

        assert_write p.decls, <<~RBS
          module RBS
            module RuntimePrototypeTest
              module TestForTypeParameters
                class C < Hash[untyped, untyped]
                end

                class C2
                  include Enumerable[untyped]
                end

                module M
                  HASH: Hash[untyped, untyped]
                end
              end
            end
          end
        RBS
      end
    end
  end

  class TestForInitialize
    def initialize() 'foo' end
  end

  def test_for_initialize_type
    SignatureManager.new do |manager|
      manager.build do |env|
        p = Runtime.new(patterns: ["RBS::RuntimePrototypeTest::TestForInitialize"], env: env, merge: true)

        assert_write p.decls, <<~RBS
          module RBS
            module RuntimePrototypeTest
              class TestForInitialize
                private

                def initialize: () -> void
              end
            end
          end
        RBS
      end
    end
  end
end
