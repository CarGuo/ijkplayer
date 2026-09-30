/* Exercise LAS's real custom AVIO ownership across open, close and failure. */
#include "../../ijkmedia/ijkplayer/ijkavformat/ijklas.c"
#include <sys/stat.h>

static void load_tag(PlayList *p, const char *path)
{
    struct stat st;
    assert(stat(path, &st) == 0);
    FlvTag_dealloc(&p->reading_tag);
    assert(FlvTag_alloc_buffer(p, &p->reading_tag, st.st_size) == 0);
    FILE *f = fopen(path, "rb");
    assert(f && fread(p->reading_tag.buf, 1, st.st_size, f) == (size_t)st.st_size);
    fclose(f);
    p->reading_tag.buf_write_offset = st.st_size;
    p->reading_tag.switch_index = 0;
    p->reading_tag.rep_index = 0;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    LasContext las = { 0 };
    PlayList *p = &las.playlist;
    AVFormatContext *outer = avformat_alloc_context();
    outer->priv_data = &las;
    p->outermost_ctx = outer;
    p->rw_mutex = SDL_CreateMutex();
    p->reading_tag_mutex = SDL_CreateMutex();
    p->tag_queue.mutex = SDL_CreateMutex();
    p->tag_queue.cond = SDL_CreateCond();
    p->tag_queue.abort_request = 1; /* finite data: return EOF after current tag */
    Representation rep = { 0 };
    p->adaptation_set.n_representation = 1;
    p->adaptation_set.representations[0] = &rep;
    snprintf(p->gop_reader.realtime_url, sizeof(p->gop_reader.realtime_url), "%s", argv[1]);
    for (int i = 0; i < 2; i++) {
        load_tag(p, argv[1]);
        assert(PlayList_open_rep(p, &p->reading_tag, outer) == 0);
        assert(p->pb && p->ctx && p->ctx->pb == p->pb);
        assert(p->ctx->flags & AVFMT_FLAG_CUSTOM_IO);
        assert(outer->nb_streams == 1 && p->is_stream_ever_opened);
        PlayList_close_rep(p);
        assert(!p->pb && !p->ctx);
    }
    assert(las.stream_reopened);
    /* Short invalid media must release both the AVIO buffer and its context. */
    assert(FlvTag_alloc_buffer(p, &p->reading_tag, 3) == 0);
    memset(p->reading_tag.buf, 0, 3);
    p->reading_tag.buf_write_offset = 3;
    assert(PlayList_open_rep(p, &p->reading_tag, outer) < 0);
    assert(!p->pb && !p->ctx);
    PlayList_close_rep(p);
    FlvTag_dealloc(&p->reading_tag);
    SDL_DestroyMutex(p->rw_mutex);
    SDL_DestroyMutex(p->reading_tag_mutex);
    SDL_DestroyMutex(p->tag_queue.mutex);
    SDL_DestroyCond(p->tag_queue.cond);
    outer->priv_data = NULL;
    avformat_free_context(outer);
    puts("PASS LAS: public AVIO allocation, repeated reopen, ownership, failure cleanup");
    return 0;
}
