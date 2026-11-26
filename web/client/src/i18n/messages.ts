import type { Message, MessageKey } from "./ru";

type Messages = Partial<Record<MessageKey, Message>>;
export const messages: Record<"ru" | "en", Messages> = { ru: {}, en: {} };

export function registerMessages(ru: Messages, en: Messages) {
  Object.assign(messages.ru, ru);
  Object.assign(messages.en, en);
}
