declare module "*/webauthn.js" {
  export function factorAnswer(
    options: import("../../../../build/contracts/api").components["schemas"]["FactorChallenge"],
    username: string,
  ): Promise<
    import("../../../../build/contracts/api").components["schemas"]["FactorAnswer"]
  >;
}
