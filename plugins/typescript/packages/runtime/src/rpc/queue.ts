/** Amortized O(1) FIFO queue for hot paths where Array.shift() becomes quadratic. */
export class FifoQueue<T> {
  private readonly values: T[] = [];
  private head = 0;

  get length(): number {
    return this.values.length - this.head;
  }

  push(value: T): void {
    this.values.push(value);
  }

  shift(): T | undefined {
    if (this.head >= this.values.length) return undefined;
    const value = this.values[this.head]!;
    this.head++;
    if (this.head >= 1_024 && this.head * 2 >= this.values.length) {
      this.values.splice(0, this.head);
      this.head = 0;
    }
    return value;
  }

  clear(): void {
    this.values.length = 0;
    this.head = 0;
  }
}
