import type { BebopCodec } from "../codec.js";
import type { BebopViewCodec } from "../reflection.js";
import type { MethodInfo, MethodType, ServiceInfo } from "../rpc.bb.js";
import type { BebopRouterBuilder } from "./router.js";

export type BebopServiceMethod<Request, Response, RequestView = Request> = {
  readonly id: number;
  readonly name: string;
  readonly methodType: MethodType;
  readonly request: BebopViewCodec<Request, RequestView>;
  readonly response: BebopCodec<Response>;
  readonly requestTypeUrl: string;
  readonly responseTypeUrl: string;
};

export type AnyBebopServiceMethod = BebopServiceMethod<unknown, unknown, unknown>;

export type Awaitable<Value> = Value | PromiseLike<Value>;

export type StreamSource<Value> =
  | Iterable<Value>
  | AsyncIterable<Value>
  | ReadableStream<Value>;

export interface BebopServiceDecorator<Handler extends object> {
  <Class extends abstract new (...args: never[]) => Handler>(
    value: Class,
    context: ClassDecoratorContext<Class>,
  ): void;
}

export type BebopServiceDefinition<
  Methods extends Readonly<Record<string, AnyBebopServiceMethod>> = Readonly<Record<string, AnyBebopServiceMethod>>,
  Handler extends object = object,
> = {
  readonly serviceName: string;
  readonly methods: Methods;
  readonly serviceInfo: ServiceInfo;
  readonly handler: BebopServiceDecorator<Handler>;
  register(builder: BebopRouterBuilder, handler: Handler): BebopRouterBuilder;
};

type ErasedServiceBinding = {
  readonly serviceName: string;
  readonly register: (builder: BebopRouterBuilder, handler: object) => BebopRouterBuilder;
};

const serviceBindings = new WeakMap<object, readonly ErasedServiceBinding[]>();

export function defineService<
  const Methods extends Readonly<Record<string, AnyBebopServiceMethod>>,
  Handler extends object,
>(
  serviceName: string,
  methods: Methods,
  registerMethods: (
    builder: BebopRouterBuilder,
    handler: Handler,
    methods: Methods,
  ) => BebopRouterBuilder,
): BebopServiceDefinition<Methods, Handler> {
  const serviceInfo: ServiceInfo = {
    name: serviceName,
    methods: Object.values(methods).map(methodInfo),
  };
  const definition: BebopServiceDefinition<Methods, Handler> = {
    serviceName,
    methods,
    serviceInfo,
    handler(value, context) {
      if (context.kind !== "class") throw new TypeError("Bebop service decorators can only decorate classes");
      const binding: ErasedServiceBinding = {
        serviceName,
        register: (builder, instance) => definition.register(builder, instance as Handler),
      };
      const existing = serviceBindings.get(value) ?? [];
      if (existing.some(({ serviceName: registeredName }) => registeredName === serviceName)) {
        throw new TypeError(`service '${serviceName}' decorates this handler class more than once`);
      }
      serviceBindings.set(value, [...existing, binding]);
    },
    register(builder, handler) {
      builder.addService(definition);
      return registerMethods(builder, handler, methods);
    },
  };
  return definition;
}

export function registeredServices(handler: object): readonly ErasedServiceBinding[] | undefined {
  return serviceBindings.get(handler.constructor);
}

export function methodInfo(method: AnyBebopServiceMethod): MethodInfo {
  return {
    name: method.name,
    methodId: method.id,
    methodType: method.methodType,
    requestTypeUrl: method.requestTypeUrl,
    responseTypeUrl: method.responseTypeUrl,
  };
}
