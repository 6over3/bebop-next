export class BebopRuntimeError extends Error {
  constructor(message: string, options?: ErrorOptions) {
    super(message, options);
    this.name = "BebopRuntimeError";
  }
}
